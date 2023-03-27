#include "napi.h"
#include "common.h"
#include "common-sdl.h"
#include "whisper.h"

// #include <node.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdint>

//  500 -> 00:05.000
// 6000 -> 01:00.000
std::string to_timestamp(int64_t t)
{
    int64_t sec = t / 100;
    int64_t msec = t - sec * 100;
    int64_t min = sec / 60;
    sec = sec - min * 60;

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d.%03d", (int)min, (int)sec, (int)msec);

    return std::string(buf);
}

// command-line parameters
struct whisper_params
{
    int32_t n_threads = std::min(4, (int32_t)std::thread::hardware_concurrency());
    int32_t step_ms = 3000;
    int32_t length_ms = 10000;
    int32_t keep_ms = 200;
    int32_t capture_id = -1;
    int32_t max_tokens = 32;
    int32_t audio_ctx = 0;

    float vad_thold = 0.6f;
    float freq_thold = 100.0f;

    bool speed_up = false;
    bool translate = false;
    bool print_special = false;
    bool no_context = true;
    bool no_timestamps = false;

    std::string language = "en";
    std::string model = "models/ggml-base.en.bin";
    std::string fname_out;
};

// Napi::Object whisper(const Napi::CallbackInfo &info)
// {
//     Napi::Env env = info.Env();
//     if (info.Length() <= 0 || !info[0].IsObject())
//     {
//         Napi::TypeError::New(env, "object expected").ThrowAsJavaScriptException();
//     }
//     whisper_params params;
//     std::vector<std::vector<std::string>> result;

//     Napi::Object whisper_params = info[0].As<Napi::Object>();
//     std::string language = whisper_params.Get("language").As<Napi::String>();
//     std::string model = whisper_params.Get("model").As<Napi::String>();
//     std::string input = whisper_params.Get("fname_inp").As<Napi::String>();

//     params.language = language;
//     params.model = model;
//     params.fname_inp.emplace_back(input);

//     // run model
//     run(params, result);

//     fprintf(stderr, "RESULT:\n");
//     for (auto sentence : result)
//     {
//         fprintf(stderr, "t0: %s, t1: %s, content: %s \n",
//                 sentence[0].c_str(), sentence[1].c_str(), sentence[2].c_str());
//     }

//     Napi::Object res = Napi::Array::New(env, result.size());
//     for (uint64_t i = 0; i < result.size(); ++i)
//     {
//         Napi::Object tmp = Napi::Array::New(env, 3);
//         for (uint64_t j = 0; j < 3; ++j)
//         {
//             tmp[j] = Napi::String::New(env, result[i][j]);
//         }
//         res[i] = tmp;
//     }

//     return res;
// }

// ThreadSafeFunction context structure
struct CallbackContext
{
    Napi::ThreadSafeFunction tsfn;
};

// thread function that performs the time-consuming task
void WorkerThread(const Napi::ThreadSafeFunction &tsfn)
{
    // simulate a time-consuming task
    // std::this_thread::sleep_for(3s);

    // create a callback function
    // auto callback = [](Napi::Env env, Napi::Function jsCallback, const std::string &str)
    // {
    //     jsCallback.Call({Napi::String::New(env, str)});
    // };

    // create a Napi::ThreadSafeFunction callback context
    // auto context = new CallbackContext{tsfn};

    // call the callback function with the result string using the Napi::ThreadSafeFunction
    // tsfn.NonBlockingCall(context, callback, "Result string");
    const char *text = "Hello from thread";
    tsfn.NonBlockingCall([text](Napi::Env env, Napi::Function jsCallback)
                         {
          Napi::String jsString = Napi::String::New(env, text);
          jsCallback.Call({ jsString }); });
}

Napi::Value StartThread(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    Napi::Function callback = info[0].As<Napi::Function>();

    auto tsfn = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "Callback",
        0, // Unlimited queue
        1);

    // create a std::thread to run the worker function in a separate thread
    std::thread workerThread(WorkerThread, tsfn);

    // detach the std::thread so it can run in the background
    workerThread.detach();

    return Napi::Boolean::New(env, true);
}

// Napi::Object Init(Napi::Env env, Napi::Object exports)
// {
//     exports.Set(
//         Napi::String::New(env, "whisper"),
//         Napi::Function::New(env, StartThread)); // Napi::Function::New(env, whisper));
//     return exports;
// }

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set(Napi::String::New(env, "startTask"), Napi::Function::New(env, StartThread));
    return exports;
}

NODE_API_MODULE(whisper, Init);
