#include "napi.h"
#include "common.h"
#include "common-sdl.h"
#include "whisper.h"

#include <node_api.h>

// #include <node.h>
#include <iostream>
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

using namespace Napi;

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

int pc = 1;

class MyWorker : public AsyncProgressWorker<char>
{
public:
    MyWorker(Function &callback, Napi::ThreadSafeFunction tsfnToEnd)
        : AsyncProgressWorker(callback), tsfnToEnd(tsfnToEnd) {}
    // ~MyWorker() {}

    // This code will be executed on the worker thread
    void Execute(const ExecutionProgress &progress) override
    {
        whisper_params params;

        params.keep_ms = 200;    // std::min(params.keep_ms, params.step_ms);
        params.length_ms = 3000; // std::max(params.length_ms, params.step_ms);
        params.no_timestamps = !use_vad;
        params.no_context |= use_vad;
        params.max_tokens = 0;
        params.language = "en";
        params.model = "/Users/bbarrows/repos/speechCmdNode/whisper.cpp/models/ggml-base.en.bin";

        // struct whisper_context *ctx = whisper_init_from_file(params.model.c_str());
        this->ctx = whisper_init_from_file(params.model.c_str());

        // const ExecutionProgress &progress = this->progress;

        // std::thread([params, ctx, progress]()
        // {
        bool is_running = true;

        audio_async audio(params.length_ms);
        if (!audio.init(params.capture_id, WHISPER_SAMPLE_RATE))
        {
            fprintf(stderr, "%s: audio.init() failed!\n", __func__);
            // return 1;
            return;
        }

        audio.resume();

        std::vector<float> pcmf32(n_samples_30s, 0.0f);
        std::vector<float> pcmf32_old;
        std::vector<float> pcmf32_new(n_samples_30s, 0.0f);

        std::vector<whisper_token> prompt_tokens;

        {
            fprintf(stderr, "\n");

            fprintf(stderr, "%s: processing %d samples (step = %.1f sec / len = %.1f sec / keep = %.1f sec), %d threads, lang = %s, task = %s, timestamps = %d ...\n",
                    __func__,
                    n_samples_step,
                    float(n_samples_step) / WHISPER_SAMPLE_RATE,
                    float(n_samples_len) / WHISPER_SAMPLE_RATE,
                    float(n_samples_keep) / WHISPER_SAMPLE_RATE,
                    params.n_threads,
                    params.language.c_str(),
                    params.translate ? "translate" : "transcribe",
                    params.no_timestamps ? 0 : 1);

            fprintf(stderr, "%s: using VAD, will transcribe on speech activity\n", __func__);

            fprintf(stderr, "\n");
        }

        int n_iter = 0;

        // printf("[Start speaking]");

        auto t_last = std::chrono::high_resolution_clock::now();
        const auto t_start = t_last;

        bool should_end = false;

        int cc = 0;
        // main audio loop
        while (is_running && !should_end)
        {
            cc++;
            if (cc % 30 == 0)
            {
                should_end = tsfnToEnd.NonBlockingCall([&should_end](Napi::Env env, Napi::Function jsCallback) -> Napi::Boolean
                                                       { 
                            Napi::Boolean sent =  jsCallback.Call({}).As<Napi::Boolean>();
                            //printf("\n\n~~~~sent: %d", sent.Value());
                            should_end = sent.Value();
                            return sent; });
            }
            // printf("\nshould_end: %d\n", should_end);

            // printf("in loop \n");
            //  handle Ctrl + C
            is_running = sdl_poll_events();

            if (!is_running)
            {
                break;
            }

            const auto t_now = std::chrono::high_resolution_clock::now();
            const auto t_diff = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_last).count();

            if (t_diff < 2000)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                continue;
            }

            // printf("audio get \n");
            audio.get(2000, pcmf32_new);

            if (::vad_simple(pcmf32_new, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, false))
            {
                // printf("vad simple called \n");
                audio.get(params.length_ms, pcmf32);
                // printf("audio get called \n");
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                // printf("somethign bad happened \n");
                continue;
            }

            t_last = t_now;
            // }

            // printf("run the interference\n");
            //  run the inference
            {
                whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                // printf("run whisper_full_default_params\n");

                wparams.print_progress = false;
                wparams.print_special = params.print_special;
                wparams.print_realtime = false;
                wparams.print_timestamps = !params.no_timestamps;
                wparams.translate = params.translate;
                wparams.no_context = true;
                wparams.single_segment = !use_vad;
                wparams.max_tokens = params.max_tokens;
                wparams.language = params.language.c_str();
                wparams.n_threads = params.n_threads;

                wparams.audio_ctx = params.audio_ctx;
                wparams.speed_up = params.speed_up;

                // disable temperature fallback
                wparams.temperature_inc = -1.0f;

                // printf("params.no_context: %d\n", params.no_context);
                wparams.prompt_tokens = params.no_context ? nullptr : prompt_tokens.data();
                wparams.prompt_n_tokens = params.no_context ? 0 : prompt_tokens.size();

                // printf("before whisper full\n");

                if (whisper_full(this->ctx, wparams, pcmf32.data(), pcmf32.size()) != 0)
                {
                    fprintf(stderr, "failed to process audio\n");
                    return;
                }

                // printf("using whisper_full is created\n");

                // print result;
                {

                    const int64_t t1 = (t_last - t_start).count() / 1000000;
                    const int64_t t0 = std::max(0.0, t1 - pcmf32.size() * 1000.0 / WHISPER_SAMPLE_RATE);

                    // printf("\n");
                    // printf("### Transcription %d START | t0 = %d ms | t1 = %d ms\n", n_iter, (int)t0, (int)t1);
                    // printf("\n");
                    //  }

                    const int n_segments = whisper_full_n_segments(this->ctx);
                    // printf("After full ng segments\n");
                    for (int i = 0; i < n_segments; ++i)
                    {
                        const char *text = whisper_full_get_segment_text(this->ctx, i);
                        // printf("before progress: %s\n", text);
                        if (text)
                        {
                            progress.Send(text, strlen(text));
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // TODO:  REMOE
                        // printf("afters progress\n");
                    }

                    // printf("\n");
                    // printf("### Transcription %d END\n", n_iter);
                }

                ++n_iter;
            }
        }
        // printf("\nOUTSIDE OF WHIEL LOOP\n");
        audio.pause();
        //         })
        // .detach();
        // .join();

        // printf("\nDetached thread\n");

        // whisper_print_timings(ctx);
        // whisper_free(this->ctx);

        // return Napi::Boolean::New(Env(), true);
    }

    void OnError(const Error &e) override
    {
        HandleScope scope(Env());
        // Pass error onto JS, no data for other parameters
        Callback().Call({String::New(Env(), e.Message())});

        whisper_free(this->ctx);

        this->tsfnToEnd.Release();
    }
    void OnOK() override
    {
        HandleScope scope(Env());
        // Pass no error, give back original data
        // printf("In On OK");
        Callback().Call({Env().Null(), String::New(Env(), "DONE in OnOK")});

        // whisper_print_timings(ctx);
        whisper_free(this->ctx);

        this->tsfnToEnd.Release();
    }
    void OnProgress(const char *data, size_t count) override
    {
        // printf("In Progress");
        HandleScope scope(Env());
        // Pass no error, no echo data, but do pass on the progress data
        // Callback().Call({Env().Null(), Env().Null(), Number::New(Env(), *data)});
        Callback().Call({Env().Null(), String::New(Env(), data)});
    }

private:
    struct whisper_context *ctx;
    Napi::ThreadSafeFunction tsfnToEnd;
};

Napi::Value MyFunction(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (info.Length() < 3) //  || !info[1].IsFunction() || !info[2].IsFunction())
    {
        Napi::TypeError::New(env, "Must be 3 args, config, callback to stop").ThrowAsJavaScriptException();
        return env.Null();
    }

    // params.keep_ms = 200;    // std::min(params.keep_ms, params.step_ms);
    // params.length_ms = 3000; // std::max(params.length_ms, params.step_ms);
    // params.no_timestamps = !use_vad;
    // params.no_context |= use_vad;
    // params.max_tokens = 0;

    Napi::Object whisper_params = info[0].As<Napi::Object>();
    std::string language = whisper_params.Get("language").As<Napi::String>();
    std::string model = whisper_params.Get("model").As<Napi::String>();
    // std::string input = whisper_params.Get("fname_inp").As<Napi::String>();

    Napi::Function callback = info[1].As<Napi::Function>();
    Napi::Function callbackToEnd = info[2].As<Napi::Function>();

    auto tsfnToEnd = Napi::ThreadSafeFunction::New(
        env,
        callbackToEnd,
        "ResultCallback",
        0, // Unlimited queue
        1);

    // auto tsfn = Napi::ThreadSafeFunction::New(
    //     env,
    //     callback,
    //     "Callback",
    //     0, // Unlimited queue
    //     1);

    // params.language = language;
    // params.model = model;

    MyWorker *worker = new MyWorker(callback, tsfnToEnd);
    // Napi::AsyncProgressWorker *async_worker = static_cast<Napi::AsyncProgressWorker *>(worker);
    worker->Queue();
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
