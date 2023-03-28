const SegfaultHandler = require('segfault-handler');
SegfaultHandler.registerHandler('crash.log');

const path = require("path");
const addon = require(path.join(
    __dirname,
    "./whisper.cpp/build/Release/whisper-addon.node"
));
const { promisify } = require("util");


const whisperParams = {
    language: "en",
    model: path.join(__dirname, "whisper.cpp/models/ggml-base.en.bin"),
    // fname_inp: "./hello.wav",
};


console.log("whisperParams =", whisperParams);

let shouldEndNow = false;
function shouldEnd() {
    // return true;
    console.error("Shoudl end was called: ", shouldEndNow)
    return shouldEndNow;
}


setTimeout(() => { console.log("*****timeout called****"); shouldEndNow = true; }, 1000000)

addon.startTask(whisperParams, (err, str) => {
    console.log("Value from whisper: ", str);
}, shouldEnd);
