#include <jni.h>

#include "AndroidOut.h"
#include "Renderer.h"

#include <game-activity/GameActivity.cpp>
#include <game-text-input/gametextinput.cpp>

#include <campello_gpu/device.hpp>
#include <campello_gpu/view.hpp>

extern "C" {

#include <game-activity/native_app_glue/android_native_app_glue.c>

using namespace systems::leal::campello_gpu;

/*!
 * Handles commands sent to this Android application
 * @param pApp the app the commands are coming from
 * @param cmd the command to handle
 */
void handle_cmd(android_app *pApp, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: {
            // A new window is created, associate a renderer with it. You may replace this with a
            // "game" class if that suits your needs. Remember to change all instances of userData
            // if you change the class here as a reinterpret_cast is dangerous this in the
            // android_main function and the APP_CMD_TERM_WINDOW handler case.
            //pApp->userData = new Renderer(pApp);
            aout << "systems::leal::campello_gpu" << std::endl;

            auto device = Device::createDefaultDevice(pApp->window);
            if (device != nullptr) {

                pApp->userData = new std::shared_ptr<Device>(device);

                auto buffer1 = device->createBuffer(10, BufferUsage::vertex);
                uint8_t buffer[256 * 256 * 4];
                auto status = buffer1->upload(0, 10, &buffer);
                aout << "upload buffer status: " << status << std::endl;

                auto buffer2 = device->createBuffer(10, BufferUsage::vertex, buffer);
                aout << "buffer2: " << buffer2 << std::endl;

                auto texture1 = device->createTexture(TextureType::tt2d, PixelFormat::rgba8uint, 256, 256, 1, 1, 1, TextureUsage::textureBinding);
                aout << "texture1: " << texture1 << std::endl;

                status = texture1->upload(0, 256 * 256 * 4, buffer);
                aout << "upload texture status: " << status << std::endl;

                auto assetManager = pApp->activity->assetManager;
                auto asset = AAssetManager_open(assetManager, "shader.spv", AASSET_MODE_BUFFER);
                auto assetLength = AAsset_getLength(asset);
                auto assetData = AAsset_getBuffer(asset);
                auto shaderModule = device->createShaderModule((uint8_t *)assetData, assetLength);
                AAsset_close(asset);

                RenderPipelineDescriptor pipelineDescriptor;
                pipelineDescriptor.vertex.module = shaderModule;
                pipelineDescriptor.vertex.entryPoint = "vertMain";
                FragmentDescriptor fd = {};
                fd.module = shaderModule;
                fd.entryPoint = "fragMain";
                pipelineDescriptor.fragment = fd;
                auto renderPipeline = device->createRenderPipeline(pipelineDescriptor);
                aout << "renderPipeline: " << renderPipeline << std::endl;
            }
            break;
        }
        case APP_CMD_TERM_WINDOW:
            if (pApp->userData) {
                auto *device = reinterpret_cast<std::shared_ptr<Device> *>(pApp->userData);
                pApp->userData = nullptr;
                delete device;
            }
            break;
        default:
            break;
    }
}

/*!
 * Enable the motion events you want to handle; not handled events are
 * passed back to OS for further processing. For this example case,
 * only pointer and joystick devices are enabled.
 *
 * @param motionEvent the newly arrived GameActivityMotionEvent.
 * @return true if the event is from a pointer or joystick device,
 *         false for all other input devices.
 */
bool motion_event_filter_func(const GameActivityMotionEvent *motionEvent) {
    auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
    return (sourceClass == AINPUT_SOURCE_CLASS_POINTER ||
            sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK);
}

/*!
 * This the main entry point for a native activity
 */
void android_main(struct android_app *pApp) {

    // Register an event handler for Android events
    pApp->onAppCmd = handle_cmd;

    // Set input event filters (set it to NULL if the app wants to process all inputs).
    // Note that for key inputs, this example uses the default default_key_filter()
    // implemented in android_native_app_glue.c.
    android_app_set_motion_event_filter(pApp, motion_event_filter_func);

    // This sets up a typical game/event loop. It will run until the app is destroyed.
    do {
        // Process all pending events before running game logic.
        bool done = false;
        while (!done) {
            // 0 is non-blocking.
            int timeout = 0;
            int events;
            android_poll_source *pSource;
            int result = ALooper_pollOnce(timeout, nullptr, &events,
                                          reinterpret_cast<void**>(&pSource));
            switch (result) {
                case ALOOPER_POLL_TIMEOUT:
                    [[clang::fallthrough]];
                case ALOOPER_POLL_WAKE:
                    // No events occurred before the timeout or explicit wake. Stop checking for events.
                    done = true;
                    break;
                case ALOOPER_EVENT_ERROR:
                    aout << "ALooper_pollOnce returned an error" << std::endl;
                    break;
                case ALOOPER_POLL_CALLBACK:
                    break;
                default:
                    if (pSource) {
                        pSource->process(pApp, pSource);
                    }
            }
        }

        // Check if any user data is associated. This is assigned in handle_cmd
        if (pApp->userData) {
            // We know that our user data is a Renderer, so reinterpret cast it. If you change your
            // user data remember to change it here
            //auto *pRenderer = reinterpret_cast<Renderer *>(pApp->userData);

            // Process game input
            //pRenderer->handleInput();

            // Render a frame
            //pRenderer->render();

            aout << "Render..." << std::endl;
        }
    } while (!pApp->destroyRequested);
}
}