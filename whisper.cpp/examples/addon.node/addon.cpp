#include "napi.h"
#include "common.h"
#include "common-sdl.h"
#include "whisper.h"

#include <node_api.h>

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
#include <ostream>

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

    int32_t n_processors = 1;
    int32_t offset_t_ms = 0;
    int32_t offset_n = 0;
    int32_t duration_ms = 0;
    int32_t max_context = -1;
    int32_t max_len = 0;
    int32_t best_of = 5;
    int32_t beam_size = -1;

    float word_thold = 0.01f;
    float entropy_thold = 2.4f;
    float logprob_thold = -1.0f;

    bool speed_up = false;
    bool translate = false;
    bool diarize = false;
    bool output_txt = false;
    bool output_vtt = false;
    bool output_srt = false;
    bool output_wts = false;
    bool output_csv = false;
    bool print_special = false;
    bool no_context = true;
    bool print_colors = false;
    bool print_progress = false;
    bool no_timestamps = false;

    std::string language = "en";
    std::string prompt;
    std::string model = "../../ggml-large.bin";
};

const int n_samples_step = (1e-3 * 3000) * WHISPER_SAMPLE_RATE;
const int n_samples_len = (1e-3 * 10000) * WHISPER_SAMPLE_RATE;
const int n_samples_keep = (1e-3 * 200) * WHISPER_SAMPLE_RATE;
const int n_samples_30s = (1e-3 * 30000.0) * WHISPER_SAMPLE_RATE;

const bool use_vad = true; // n_samples_step <= 0; // sliding window mode uses VAD

const int n_new_line = !use_vad ? std::max(1, 10000 / 3000 - 1) : 1; // number of steps to print new line

class MyWorker : public Napi::AsyncWorker
{
public:
    MyWorker(Napi::Function &callback, Napi::Function &stopCallback)
        : Napi::AsyncWorker(callback), stopCallback_(stopCallback)
    {
    }

    void Execute() override
    {
        // Do some work in a separate thread
        for (int i = 0; i < 10; i++)
        {
            if (stopped_)
            {
                // Stop the worker if the stopCallback was called
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            result_ = i * 2;
        }
    }

    void OnOK() override
    {
        // Call the JavaScript callback function with the result
        Napi::HandleScope scope(Env());
        auto hw = Napi::String::New(this->Env(), "hello from Worker");
        Callback().Call({hw});
    }

    void OnError(Napi::Error const &error) override
    {
        // Call the JavaScript callback function with the error
        Napi::HandleScope scope(Env());
        Callback().Call({error.Value()});
        cerr << "ERROR: " << error.Value() << endl;
        // fprintf(stderr, "RESULT:\n");
    }

    void Stop()
    {
        stopped_ = true;
    }

private:
    int result_;
    Napi::Function stopCallback_;
    bool stopped_ = false;
};

void StopWorker(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    MyWorker *worker = static_cast<MyWorker *>(info.Data());
    worker->Stop();
}

Napi::Value MyFunction(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 3) //  || !info[1].IsFunction() || !info[2].IsFunction())
    {
        Napi::TypeError::New(env, "Must be 3 args, config, callback with data, function to stop thread").ThrowAsJavaScriptException();
        return env.Null();
    }

    whisper_params params;
    params.keep_ms = 200;    // std::min(params.keep_ms, params.step_ms);
    params.length_ms = 3000; // std::max(params.length_ms, params.step_ms);
    params.no_timestamps = !use_vad;
    params.no_context |= use_vad;
    params.max_tokens = 0;

    Napi::Object whisper_params = info[0].As<Napi::Object>();
    std::string language = whisper_params.Get("language").As<Napi::String>();
    std::string model = whisper_params.Get("model").As<Napi::String>();
    // std::string input = whisper_params.Get("fname_inp").As<Napi::String>();

    Napi::Function callback = info[1].As<Napi::Function>();
    Napi::Function stopCallback = info[2].As<Napi::Function>();

    params.language = language;
    params.model = model;

    MyWorker *worker = new MyWorker(callback, stopCallback);
    Napi::AsyncWorker *async_worker = static_cast<Napi::AsyncWorker *>(worker);
    async_worker->Queue();
    // Napi::ObjectReference *objRef = new Napi::ObjectReference(env, Napi::Persistent(info.This()));
    // async_worker->Queue(objRef, StopWorker);

    return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set(Napi::String::New(env, "startTask"), Napi::Function::New(env, MyFunction));
    return exports;
}

NODE_API_MODULE(addon, Init)
