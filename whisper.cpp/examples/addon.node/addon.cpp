/*
please write me c++ node add on example code  that returns a function which takes a javascript function as an argument that returns a boolean, I want to call this function from a thread periodically to see if I should stop the thread.



*/

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
    Napi::ThreadSafeFunction tsfnToEnd;
};

const int n_samples_step = (1e-3 * 3000) * WHISPER_SAMPLE_RATE;
const int n_samples_len = (1e-3 * 10000) * WHISPER_SAMPLE_RATE;
const int n_samples_keep = (1e-3 * 200) * WHISPER_SAMPLE_RATE;
const int n_samples_30s = (1e-3 * 30000.0) * WHISPER_SAMPLE_RATE;

const bool use_vad = true; // n_samples_step <= 0; // sliding window mode uses VAD

const int n_new_line = !use_vad ? std::max(1, 10000 / 3000 - 1) : 1; // number of steps to print new line

Napi::Value StartThread(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    whisper_params params;

    // if (whisper_params_parse(argc, argv, params) == false)
    // {
    //     return 1;
    // }

    params.keep_ms = 200;    // std::min(params.keep_ms, params.step_ms);
    params.length_ms = 3000; // std::max(params.length_ms, params.step_ms);

    params.no_timestamps = !use_vad;
    params.no_context |= use_vad;
    params.max_tokens = 0;

    //     whisper_params params;
    //     std::vector<std::vector<std::string>> result;

    Napi::Object whisper_params = info[0].As<Napi::Object>();
    std::string language = whisper_params.Get("language").As<Napi::String>();
    std::string model = whisper_params.Get("model").As<Napi::String>();
    // std::string input = whisper_params.Get("fname_inp").As<Napi::String>();

    params.language = language;
    params.model = model;
    // params.fname_inp.emplace_back(input);

    // fprintf(stderr, "\n");
    // fprintf(stderr, "usage: %s [options]\n", argv[0]);
    // fprintf(stderr, "\n");
    // fprintf(stderr, "options:\n");
    // fprintf(stderr, "  -h,       --help          [default] show this help message and exit\n");
    // fprintf(stderr, "  -t N,     --threads N     [%-7d] number of threads to use during computation\n", params.n_threads);
    // fprintf(stderr, "            --step N        [%-7d] audio step size in milliseconds\n", params.step_ms);
    // fprintf(stderr, "            --length N      [%-7d] audio length in milliseconds\n", params.length_ms);
    // fprintf(stderr, "            --keep N        [%-7d] audio to keep from previous step in ms\n", params.keep_ms);
    // fprintf(stderr, "  -c ID,    --capture ID    [%-7d] capture device ID\n", params.capture_id);
    // fprintf(stderr, "  -mt N,    --max-tokens N  [%-7d] maximum number of tokens per audio chunk\n", params.max_tokens);
    // fprintf(stderr, "  -ac N,    --audio-ctx N   [%-7d] audio context size (0 - all)\n", params.audio_ctx);
    // fprintf(stderr, "  -vth N,   --vad-thold N   [%-7.2f] voice activity detection threshold\n", params.vad_thold);
    // fprintf(stderr, "  -fth N,   --freq-thold N  [%-7.2f] high-pass frequency cutoff\n", params.freq_thold);
    // fprintf(stderr, "  -su,      --speed-up      [%-7s] speed up audio by x2 (reduced accuracy)\n", params.speed_up ? "true" : "false");
    // fprintf(stderr, "  -tr,      --translate     [%-7s] translate from source language to english\n", params.translate ? "true" : "false");
    // fprintf(stderr, "  -ps,      --print-special [%-7s] print special tokens\n", params.print_special ? "true" : "false");
    // fprintf(stderr, "  -kc,      --keep-context  [%-7s] keep context between audio chunks\n", params.no_context ? "false" : "true");
    // fprintf(stderr, "  -l LANG,  --language LANG [%-7s] spoken language\n", params.language.c_str());
    // fprintf(stderr, "  -m FNAME, --model FNAME   [%-7s] model path\n", params.model.c_str());
    // fprintf(stderr, "  -f FNAME, --file FNAME    [%-7s] text output file name\n", params.fname_out.c_str());
    // fprintf(stderr, "\n");

    Napi::Function callback = info[1].As<Napi::Function>();

    auto tsfn = Napi::ThreadSafeFunction::New(
        env,
        callback,
        "Callback",
        0, // Unlimited queue
        1);

    Napi::Function callbackToEnd = info[2].As<Napi::Function>();

    auto tsfnToEnd = Napi::ThreadSafeFunction::New(
        env,
        callbackToEnd,
        "ResultCallback",
        0, // Unlimited queue
        1);

    // whisper init

    if (whisper_lang_id(params.language.c_str()) == -1)
    {
        fprintf(stderr, "error: unknown language '%s'\n", params.language.c_str());
        // whisper_print_usage(argc, argv, params);
        exit(0);
    }

    struct whisper_context *ctx = whisper_init_from_file(params.model.c_str());

    std::thread([tsfn, tsfnToEnd, params, ctx]()
                {
                    bool is_running = true;
                    bool should_end = false;

                                        // Simulate a long-running task
                    //   std::this_thread::sleep_for(std::chrono::seconds(1));



                    // init audio

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

                    // print some info about the processing
                    {
                        fprintf(stderr, "\n");
                        // if (!whisper_is_multilingual(ctx))
                        // {
                        //     if (params.language != "en" || params.translate)
                        //     {
                        //         params.language = "en";
                        //         params.translate = false;
                        //         fprintf(stderr, "%s: WARNING: model is not multilingual, ignoring language and translation options\n", __func__);
                        //     }
                        // }
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

                        // if (!use_vad)
                        // {
                        //     fprintf(stderr, "%s: n_new_line = %d, no_context = %d\n", __func__, n_new_line, params.no_context);
                        // }
                        // else
                        // {
                        fprintf(stderr, "%s: using VAD, will transcribe on speech activity\n", __func__);
                        // }

                        fprintf(stderr, "\n");
                    }

                    int n_iter = 0;


                    std::ofstream fout;
                    // if (params.fname_out.length() > 0)
                    // {
                    //     fout.open(params.fname_out);
                    //     if (!fout.is_open())
                    //     {
                    //         fprintf(stderr, "%s: failed to open output file '%s'!\n", __func__, params.fname_out.c_str());
                    //         return;
                    //     }
                    // }

                    printf("[Start speaking]");
                    // fflush(stdout);

                    auto t_last = std::chrono::high_resolution_clock::now();
                    const auto t_start = t_last;

                    // main audio loop
                    while (is_running && !should_end)
                    {

                        

                        // auto resultContext = new CallbackContext{tsfnToEnd};
                        should_end = tsfnToEnd.NonBlockingCall([&should_end](Napi::Env env, Napi::Function jsCallback) -> Napi::Boolean
                                                               { 
                            Napi::Boolean sent =  jsCallback.Call({}).As<Napi::Boolean>();
                            printf("\n\n~~~~sent: %d", sent.Value());
                            should_end = sent.Value();
                            return sent;
                        });

                        printf("\nshould_end: %d\n", should_end);

                        printf("in loop \n");
                        // handle Ctrl + C
                        is_running = sdl_poll_events();

                        if (!is_running)
                        {
                            break;
                        }

                        // process new audio

                        // if (!use_vad)
                        // {
                        //     while (true)
                        //     {
                        //         audio.get(params.step_ms, pcmf32_new);

                        //         if ((int)pcmf32_new.size() > 2 * n_samples_step)
                        //         {
                        //             fprintf(stderr, "\n\n%s: WARNING: cannot process audio fast enough, dropping audio ...\n\n", __func__);
                        //             audio.clear();
                        //             continue;
                        //         }

                        //         if ((int)pcmf32_new.size() >= n_samples_step)
                        //         {
                        //             audio.clear();
                        //             break;
                        //         }

                        //         std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        //     }

                        //     const int n_samples_new = pcmf32_new.size();

                        //     // take up to params.length_ms audio from previous iteration
                        //     const int n_samples_take = std::min((int)pcmf32_old.size(), std::max(0, n_samples_keep + n_samples_len - n_samples_new));

                        //     // printf("processing: take = %d, new = %d, old = %d\n", n_samples_take, n_samples_new, (int) pcmf32_old.size());

                        //     pcmf32.resize(n_samples_new + n_samples_take);

                        //     for (int i = 0; i < n_samples_take; i++)
                        //     {
                        //         pcmf32[i] = pcmf32_old[pcmf32_old.size() - n_samples_take + i];
                        //     }

                        //     memcpy(pcmf32.data() + n_samples_take, pcmf32_new.data(), n_samples_new * sizeof(float));

                        //     pcmf32_old = pcmf32;
                        // }
                        // else
                        // {
                        const auto t_now = std::chrono::high_resolution_clock::now();
                        const auto t_diff = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_last).count();

                        if (t_diff < 2000)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));

                            continue;
                        }

printf("audio get \n");
                        audio.get(2000, pcmf32_new);

                        if (::vad_simple(pcmf32_new, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, false))
                        {
                            printf("vad simple called \n");
                            audio.get(params.length_ms, pcmf32);
                            printf("audio get called \n");
                        }
                        else
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            printf("somethign bad happened \n");
                            continue;
                        }

                        t_last = t_now;
                        // }

                        printf("run the interference\n");
                        // run the inference
                        {
                            whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                            printf("run whisper_full_default_params\n");

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

                            printf("params.no_context: %d\n", params.no_context);
                            wparams.prompt_tokens = params.no_context ? nullptr : prompt_tokens.data();
                            wparams.prompt_n_tokens = params.no_context ? 0 : prompt_tokens.size();

                            printf("before whisper full\n");
                            
                            if (whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size()) != 0)
                            {
                                fprintf(stderr, "failed to process audio\n");
                                return;
                            }

                            printf("using whisper_full is created\n");

                            // print result;
                            {
                                // if (!use_vad)
                                // {
                                //     printf("\33[2K\r");

                                //     // print long empty line to clear the previous line
                                //     printf("%s", std::string(100, ' ').c_str());

                                //     printf("\33[2K\r");
                                // }
                                // else
                                // {
                                const int64_t t1 = (t_last - t_start).count() / 1000000;
                                const int64_t t0 = std::max(0.0, t1 - pcmf32.size() * 1000.0 / WHISPER_SAMPLE_RATE);

                                printf("\n");
                                printf("### Transcription %d START | t0 = %d ms | t1 = %d ms\n", n_iter, (int)t0, (int)t1);
                                printf("\n");
                                // }

                                const int n_segments = whisper_full_n_segments(ctx);
                                for (int i = 0; i < n_segments; ++i)
                                {
                                    const char *text = whisper_full_get_segment_text(ctx, i);

                                    // std::this_thread::sleep_for(std::chrono::milliseconds(1));

                                    tsfn.NonBlockingCall([text](Napi::Env env, Napi::Function jsCallback)
                                                         {
          Napi::String jsString = Napi::String::New(env, text);
          jsCallback.Call({ jsString }); });

                                    // auto data = std::make_shared<std::string>(text);
                                    // tsfn.NonBlockingCall(
                                    //     data,
                                    //     [](Napi::Env env, Napi::Function jsCallback, std::shared_ptr<std::string> data)
                                    //     {
                                    //         // Convert the C++ string to a JavaScript string
                                    //         Napi::String jsString = Napi::String::New(env, *data);

                                    //         // Call the JavaScript callback function with the string argument
                                    //         jsCallback.Call({jsString});

                                    //         // Since the ThreadSafeFunction is not needed anymore, we need to release the reference
                                    //         // jsCallback.Env().AsyncFromSnapshot(nullptr, tsfn.Release());
                                    //     });

                                    // if (params.no_timestamps)
                                    // {
                                    //     printf("%s", text);
                                    //     fflush(stdout);

                                    //     if (params.fname_out.length() > 0)
                                    //     {
                                    //         fout << text;
                                    //     }
                                    // }
                                    // else
                                    // {
                                    //     const int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                                    //     const int64_t t1 = whisper_full_get_segment_t1(ctx, i);

                                    //     printf("[%s --> %s]  %s\n", to_timestamp(t0).c_str(), to_timestamp(t1).c_str(), text);

                                    //     if (params.fname_out.length() > 0)
                                    //     {
                                    //         fout << "[" << to_timestamp(t0) << " --> " << to_timestamp(t1) << "]  " << text << std::endl;
                                    //     }
                                    // }
                                }

                                // if (params.fname_out.length() > 0)
                                // {
                                //     fout << std::endl;
                                // }

                                // if (use_vad)
                                // {
                                printf("\n");
                                printf("### Transcription %d END\n", n_iter);
                                // }
                            }

                            ++n_iter;

                            // if (!use_vad && (n_iter % n_new_line) == 0)
                            // {
                            //     printf("\n");

                            //     // keep part of the audio for next iteration to try to mitigate word boundary issues
                            //     pcmf32_old = std::vector<float>(pcmf32.end() - n_samples_keep, pcmf32.end());

                            //     // Add tokens of the last full length segment as the prompt
                            //     if (!params.no_context)
                            //     {
                            //         prompt_tokens.clear();

                            //         const int n_segments = whisper_full_n_segments(ctx);
                            //         for (int i = 0; i < n_segments; ++i)
                            //         {
                            //             const int token_count = whisper_full_n_tokens(ctx, i);
                            //             for (int j = 0; j < token_count; ++j)
                            //             {
                            //                 prompt_tokens.push_back(whisper_full_get_token_id(ctx, i, j));
                            //             }
                            //         }
                            //     }
                            // }
                        }

            

                    }
                    audio.pause(); })
        .detach();

    // whisper_print_timings(ctx);
    // whisper_free(ctx);

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
