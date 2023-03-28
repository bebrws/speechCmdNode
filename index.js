const { Configuration, OpenAIApi } = require("openai")


const SegfaultHandler = require('segfault-handler');
SegfaultHandler.registerHandler('crash.log');

const path = require("path");
const addon = require(path.join(
    __dirname,
    "./whisper.cpp/build/Release/whisper-addon.node"
));
const { promisify } = require("util");


const apiKey = process.env['OPENAI_API_KEY'];


let context = [];

const addContext = (text) => {
    context = [...context, text];
};

const getContext = () => {
    return context;
};


const generateCompletion = async (prompt) => {

    try {

        const configuration = new Configuration({
            apiKey,
        });

        const openai = new OpenAIApi(configuration);


        addContext({ "role": "user", "content": prompt });
        // addContext({ "role": "assistant", "content": `Read the context, when returning the answer, always placing any code at the very end of the response following the string "###"` });

        console.log(`\n\nMAking request with FULL prompt: ${JSON.stringify(getContext())}\n\n`);
        const request = await openai.createChatCompletion({
            model: "gpt-3.5-turbo",
            messages: getContext(),
        })
            .then((res) => {
                console.log(`First rest:`, JSON.stringify(res.data));
                addContext(res.data.choices[0].message);

                return res.data.choices[0].message;
            })
            .catch((err) => {
                console.error(`Error: ${err}`, JSON.stringify(err));
            });

        if (request == undefined || !request?.content) {
            throw new Error("Something went wrong with the request!");
        }

        return request;
    } catch (err) {
        console.error(`"Something went wrong!!": ${err}`, JSON.stringify(err));
    }
};



// const res = await generateCompletion('Write applescript that brings the Google Chrome window to the front and opens up the first tab for Google Meet');
// console.log(res);

generateCompletion('Write applescript that brings the Google Chrome window to the front and opens up the first tab for Google Meet').then((res) => {
    console.log(res);
    generateCompletion('Modify the applescript to mute the Google Meet by sending the keys CMD + D').then((res) => {
        console.log(res);
    });
});


// const whisperParams = {
//     language: "en",
//     model: path.join(__dirname, "whisper.cpp/models/ggml-base.en.bin"),
//     // fname_inp: "./hello.wav",
// };


// console.log("whisperParams =", whisperParams);

// let shouldEndNow = false;
// function shouldEnd() {
//     // return true;
//     console.error("Shoudl end was called: ", shouldEndNow)
//     return shouldEndNow;
// }


// setTimeout(() => { console.log("*****timeout called****"); shouldEndNow = true; }, 1000000)

// addon.startTask(whisperParams, (err, str) => {
//     console.log("Value from whisper: ", str);
// }, shouldEnd);
