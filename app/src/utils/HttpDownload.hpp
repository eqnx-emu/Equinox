#pragma once

#include <cstdint>
#include <functional>
#include <string>

// HTTPS GET over curl: verified TLS (romfs CA bundle on Switch, the system
// store elsewhere), redirects followed, HTTP errors treated as failures.

// Called while a download runs; total is 0 until the server sends a length.
// Returning false cancels the download. It runs on the downloading thread, so
// UI updates must be marshalled with brls::sync.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

bool httpGetToString(const std::string& url, std::string& out, long timeoutSec,
                     std::string& error);

// Same, and `httpStatus` receives the HTTP status the server answered with
// (0 when there was no answer). The body of an HTTP error is not kept.
bool httpGetToString(const std::string& url, std::string& out, long timeoutSec,
                     const Progress& progress, std::string& error, long& httpStatus);

// Writes the body to `path`. On failure the partial file is left for the
// caller to remove.
bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error);

// Same, and `httpStatus` receives the HTTP status the server answered with,
// or 0 when there was no answer (offline, DNS, TLS, timeout).
bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error, long& httpStatus);

// Same, and `writeFailed` is true when the target file could not be opened or
// written (full card, read-only), as opposed to a network failure.
bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error, long& httpStatus,
                   bool& writeFailed);

struct ResumableResult {
    bool ok;
    bool writeFailed;  // the .part could not be opened or written, or the disk is too small
    bool restarted;    // the server resent the whole file, so the .part began again
    long status;       // HTTP status, 0 when there was no answer
    std::string error;
};

// Downloads `url` into `partPath`, continuing a previous attempt when the
// control file `<partPath>.resume` still matches (same URL, same validator).
// There is no total timeout: the transfer fails after 30 s below 1 KiB/s.
// `progress` counts bytes from the start of the file, not of this attempt. On
// a network failure or a cancel the `.part` and control file stay for the next
// attempt; the caller removes them after a failed validation or a user cancel.
// HTTP 416 removes both. Success removes the control file only.
ResumableResult httpGetResumable(const std::string& url, const std::string& partPath,
                                 const Progress& progress);
