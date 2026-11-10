#include "emu/GameDataJob.hpp"

#include <filesystem>
#include <system_error>

#include "emu/CoverName.hpp"
#include "emu/CoverStore.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/RomCrc.hpp"

namespace fs = std::filesystem;

namespace emu {

namespace {

void removeFile(const std::string& path) {
    std::error_code ec;
    fs::remove(fromUtf8(path), ec);
}

DataStop stopFor(FetchResult result) {
    switch (result) {
        case FetchResult::Cancelled: return DataStop::Cancelled;
        case FetchResult::Disk: return DataStop::Disk;
        default: return DataStop::Offline;
    }
}

bool failed(FetchResult result) {
    return result == FetchResult::Transient || result == FetchResult::Disk ||
           result == FetchResult::Cancelled;
}

// Tries each libretro name in turn. `stop` is set only when the job must end.
PartState fetchThumb(const GameDataRequest& req, DataTransport& transport, ThumbKind kind,
                     const std::string& canonical, DataStop& stop) {
    const std::string dest = thumbPath(req.paths, kind, req.rom.title);
    const std::string part = dest + ".part";
    for (const std::string& name : thumbNameCandidates(canonical, req.rom.title)) {
        const FetchResult result = transport.toFile(thumbUrl(*req.console, kind, name), part);
        if (failed(result)) {
            removeFile(part);
            stop = stopFor(result);
            return PartState::Unknown;
        }
        if (result == FetchResult::Ok && looksLikePng(part)) {
            if (replaceFile(part, dest)) return PartState::Ok;
            removeFile(part);
            stop = DataStop::Disk;
            return PartState::Unknown;
        }
        removeFile(part);
    }
    return PartState::Missing;
}

// One ScreenScraper media into `dest`. Missing when the file is not an image.
PartState fetchMedia(DataTransport& transport, const std::string& url, const std::string& dest,
                     DataStop& stop) {
    if (url.empty()) return PartState::Missing;
    const std::string part = dest + ".part";
    const FetchResult result = transport.toFile(url, part);
    if (failed(result)) {
        removeFile(part);
        stop = stopFor(result);
        return PartState::Unknown;
    }
    if (result == FetchResult::Ok && looksLikeImage(part)) {
        if (replaceFile(part, dest)) return PartState::Ok;
        removeFile(part);
        stop = DataStop::Disk;
        return PartState::Unknown;
    }
    removeFile(part);
    return PartState::Missing;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fromUtf8(path), ec);
}

}  // namespace

float stepProgress(DataStep step) {
    switch (step) {
        case DataStep::Identify: return 0.0f;
        case DataStep::Snap: return 0.25f;
        case DataStep::Title: return 0.5f;
        case DataStep::Synopsis: return 0.75f;
        case DataStep::Done: break;
    }
    return 1.0f;
}

DataOutcome fetchGameData(const GameDataRequest& req, DataTransport& transport,
                          const std::function<bool(DataStep)>& onStep) {
    DataOutcome out;
    GameMeta& meta = out.meta;
    auto enter = [&](DataStep step) {
        if (onStep(step)) return true;
        out.stop = DataStop::Cancelled;
        return false;
    };

    if (!enter(DataStep::Identify)) return out;
    romStat(req.rom.path, meta.size, meta.mtime);
    std::string error;
    if (hashesRoms(*req.console) && romCrc32(req.rom.path, *req.console, meta.crc, error) &&
        req.db) {
        if (const GameInfo* info = req.db->find(meta.crc)) {
            meta.identified = true;
            meta.canonical = info->name;
        }
    }

    if (!enter(DataStep::Snap)) return out;
    meta.snap = fetchThumb(req, transport, ThumbKind::Snap, meta.canonical, out.stop);
    if (out.stop != DataStop::None) return out;

    if (!enter(DataStep::Title)) return out;
    meta.title = fetchThumb(req, transport, ThumbKind::Title, meta.canonical, out.stop);
    if (out.stop != DataStop::None) return out;

    if (!enter(DataStep::Synopsis)) return out;
    const int systemId = ssSystemId(*req.console);
    if (!ssConfigured(req.ss) || systemId == 0) {
        meta.synopsis = PartState::Skipped;
    } else {
        const SsQuery query{systemId, meta.crc, req.rom.fileName, meta.size};
        std::string body;
        long httpStatus = 0;
        const FetchResult result = transport.toString(ssUrl(req.ss, query), body, httpStatus);
        if (failed(result)) {
            out.stop = stopFor(result);
            return out;
        }
        const SsResult parsed =
            result == FetchResult::NotFound ? ssParse(404, body, req.ss.lang)
                                            : ssParse(httpStatus, body, req.ss.lang);
        switch (parsed.status) {
            case SsStatus::Ok:
                meta.synopsisText = parsed.synopsis;
                meta.synopsisLang = parsed.lang;
                meta.synopsis = parsed.synopsis.empty() ? PartState::Missing : PartState::Ok;
                meta.genre = parsed.genre;
                meta.developer = parsed.developer;
                meta.publisher = parsed.publisher;
                meta.players = parsed.players;
                meta.year = parsed.year;
                break;
            case SsStatus::NotFound: meta.synopsis = PartState::Missing; break;
            case SsStatus::BadLogin: out.stop = DataStop::BadLogin; return out;
            case SsStatus::Quota: out.stop = DataStop::Quota; return out;
            case SsStatus::Transient: out.stop = DataStop::Offline; return out;
        }
        // The images libretro does not have, and the cover when there is none yet.
        if (meta.snap == PartState::Missing) {
            meta.snap = fetchMedia(transport, parsed.snapUrl,
                                   thumbPath(req.paths, ThumbKind::Snap, req.rom.title), out.stop);
            if (out.stop != DataStop::None) return out;
        }
        if (meta.title == PartState::Missing) {
            meta.title = fetchMedia(transport, parsed.titleUrl,
                                    thumbPath(req.paths, ThumbKind::Title, req.rom.title), out.stop);
            if (out.stop != DataStop::None) return out;
        }
        const std::string cover = coverPath(req.paths, req.rom.title);
        if (!parsed.boxUrl.empty() && !fileExists(cover)) {
            if (fetchMedia(transport, parsed.boxUrl, cover, out.stop) == PartState::Ok)
                clearCoverMiss(req.paths, req.rom.title);
            if (out.stop != DataStop::None) return out;
        }
    }

    if (!saveMeta(metaPath(req.paths, req.rom.title), meta)) {
        out.stop = DataStop::Disk;
        return out;
    }
    out.complete = true;
    return out;
}

}  // namespace emu
