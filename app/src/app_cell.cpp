//
//  app_cell.cpp
//  Moonlight
//
//  Created by Даниил Виноградов on 03.06.2021.
//

#include "app_cell.hpp"
#include "BoxArtManager.hpp"
#include "Settings.hpp"
#include "streaming_view.hpp"

#include <borealis/core/cache_helper.hpp>
#include <borealis/extern/nanovg/stb_image.h>

namespace {
std::shared_ptr<unsigned char> decodeCover(const std::string& path, int& width,
                                           int& height) {
    int channels = 0;
    unsigned char* px = stbi_load(path.c_str(), &width, &height, &channels, 4);
    return std::shared_ptr<unsigned char>(px, [](unsigned char* p) {
        if (p)
            stbi_image_free(p);
    });
}
} // namespace

AppCell::AppCell(const Host& host, const AppInfo& app, int currentApp)
    : host(host), appId(app.app_id) {
    this->inflateFromXMLRes("xml/cells/app_cell.xml");
    this->setFavorite(false);

    title->setText(app.name);
    title->setTextColor(nvgRGB(255, 255, 255));

    bool isUnactive = currentApp != 0 && currentApp != app.app_id;
    unactiveLayer->setVisibility(isUnactive ? Visibility::VISIBLE
                                            : Visibility::GONE);
    currentAppImage->setVisibility(
        currentApp == app.app_id ? Visibility::VISIBLE : Visibility::GONE);

    this->addGestureRecognizer(new TapGestureRecognizer(this));
    this->registerClickAction([this, host, app](View* view) {
        if (beforeLaunch && !beforeLaunch())
            return true;
        auto* frame = new AppletFrame(new StreamingView(host, app));
        frame->setBackground(ViewBackground::NONE);
        frame->setHeaderVisibility(brls::Visibility::GONE);
        frame->setFooterVisibility(brls::Visibility::GONE);
        Application::pushActivity(new Activity(frame));
        return true;
    });
    this->setActionAvailable(BUTTON_A, !isUnactive);

    if (BoxArtManager::instance().has_boxart(host, app.app_id))
        loadCover();
    else if (GameStreamClient::instance().has_server_data(
                 GameStreamClient::instance().active_address(host)))
        fetchCover();
    else
        coverPending = true;
}

void AppCell::loadCover() {
    const std::string path = BoxArtManager::get_texture_path(host, appId);

    // Already uploaded for another cell (library and favorites share it).
    if (useCachedCover(path))
        return;

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, path]() {
        int width = 0, height = 0;
        auto pixels = decodeCover(path, width, height);
        brls::sync([ASYNC_TOKEN, path, pixels, width, height]() {
            ASYNC_RELEASE
            applyCover(path, pixels, width, height);
        });
    });
}

void AppCell::fetchCover() {
    coverPending = false;

    ASYNC_RETAIN
    GameStreamClient::instance().app_boxart(
        host, appId,
        [ASYNC_TOKEN, host = this->host, appId = this->appId](
            GSResult<Data> result) {
            if (!result.isSuccess()) {
                ASYNC_RELEASE
                return;
            }

            // Saving shrinks the image (decode, resize, encode): keep it off
            // the UI thread, along with decoding the result.
            Data data = result.value();
            brls::async([ASYNC_TOKEN, host, appId, data]() {
                BoxArtManager::instance().set_data(host, data, appId);
                const std::string path =
                    BoxArtManager::get_texture_path(host, appId);
                int width = 0, height = 0;
                auto pixels = decodeCover(path, width, height);
                brls::sync([ASYNC_TOKEN, path, pixels, width, height]() {
                    ASYNC_RELEASE
                    applyCover(path, pixels, width, height);
                });
            });
        });
}

void AppCell::applyCover(const std::string& path,
                         std::shared_ptr<unsigned char> pixels, int width,
                         int height) {
    // Another cell may have uploaded the same cover in the meantime.
    if (useCachedCover(path) || !pixels)
        return;

    const int texture = nvgCreateImageRGBA(Application::getNVGContext(), width,
                                           height, 0, pixels.get());
    if (texture <= 0)
        return;
    setCoverTexture(texture);
    TextureCache::instance().addCache(path, texture);
}

bool AppCell::useCachedCover(const std::string& path) {
    const int texture = TextureCache::instance().getCache(path);
    if (texture <= 0)
        return false;
    setCoverTexture(texture);
    return true;
}

// Same bookkeeping as Image::setImageFromFile: the cache owns the textures.
void AppCell::setCoverTexture(int texture) {
    if (image->getTexture() > 0)
        TextureCache::instance().removeCache(image->getTexture());
    image->setFreeTexture(false);
    image->innerSetImage(texture);
}

void AppCell::setCaptionVisible(bool visible) {
    caption->setVisibility(visible ? Visibility::VISIBLE : Visibility::GONE);
}

void AppCell::setFavorite(bool favorite) {
    favoriteAppImage->setVisibility(favorite ? Visibility::VISIBLE
                                             : Visibility::GONE);
}
