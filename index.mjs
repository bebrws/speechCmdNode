const path = require("path");
const { whisper } = require(path.join(
    __dirname,
    "../../build/Release/whisper-addon"
));
const { promisify } = require("util");

const whisperAsync = promisify(whisper);

const whisperParams = {
    language: "en",
    model: path.join(__dirname, "/Users/bbarrows/repos/speechCmdNode/whisper.cpp/models/ggml-base.en.bin"),
    fname_inp: "./hello.wav",
};

// const arguments = process.argv.slice(2);
// const params = Object.fromEntries(
//     arguments.reduce((pre, item) => {
//         if (item.startsWith("--")) {
//             return [...pre, item.slice(2).split("=")];
//         }
//         return pre;
//     }, [])
// );

// for (const key in params) {
//     if (whisperParams.hasOwnProperty(key)) {
//         whisperParams[key] = params[key];
//     }
// }

console.log("whisperParams =", whisperParams);

whisperAsync(whisperParams).then((result) => {
    console.log(`Result from whisper: ${result}`);
});


// import { SpeechRecorder } from 'speech-recorder';


// const params = {
//     filePath: "test.wav", // required
//     model: "medium",             // default
//     output: "JSON",               // default
// }



// const recorder = new SpeechRecorder({
//     onChunkStart: ({ audio }) => {
//         console.log(Date.now(), "Chunk start");
//     },
//     onAudio: ({ speaking, probability, volume }) => {
//         console.log(Date.now(), speaking, probability, volume);
//     },
//     onChunkEnd: () => {
//         console.log(Date.now(), "Chunk end");
//     },
// });

// console.log("Recording for 5 seconds...");
// recorder.start();

// await delay(2000)


// console.log("Done!");
// recorder.stop();

// const transcript = await whisper(params);

// console.log(transcript);