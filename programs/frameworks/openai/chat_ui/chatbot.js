const chatInput = document.querySelector('.chat-input textarea');
const sendChatBtn = document.querySelector('.chat-input button');
const chatbox = document.querySelector(".chatbox");

let userMessage;

const createChatLi = (message, className) => {
	const chatLi = document.createElement("li");
	chatLi.classList.add("chat", className);
	
	let chatContent = className === "chat-outgoing" ? message : message;
	chatLi.textContent = chatContent;

	return chatLi;
}

const generateResponse = (incomingChatLi) => {
	const messageElement = incomingChatLi.querySelector("p");
	
	url = "http://localhost:1187/completion";
	const requestOptions = {
        method: "POST",
        body:  userMessage,
    }

	fetch(url, requestOptions)
        .then(res => {
            if (!res.ok) {
                throw new Error("Network response was not ok");
            }
            return res.json();
        })
        .then(data => {
            messageElement.textContent = data;
            chatInput.value = "";
        })
        .catch((error) => {
        	console.error(error, error.stack);
            messageElement.classList.add("error");
            messageElement.textContent = "Oops! Something went wrong. Please try again!";
        })
        .finally(() => chatbox.scrollTo(0, chatbox.scrollHeight));
};

const handleChat = () => {
	userMessage = chatInput.value.trim();
	if(!userMessage) {
		return;
	}

	chatbox.appendChild(createChatLi(userMessage, "chat-outgoing"));
	chatbox.scrollTo(0, chatbox.scrollHeight);

	setTimeout(() => {
		const incomingChatLi = createChatLi("Thinking...", "chat-incoming")
		chatbox.appendChild(incomingChatLi);
		chatbox.scrollTo(0, chatbox.scrollHeight);
		generateResponse(incomingChatLi);
	}, 900);
}

sendChatBtn.addEventListener("click", handleChat);

function cancel() {
	let chatbotcomplete = document.querySelector(".chatBot");
	if(chatbotcomplete.style.display != 'none') {
		chatbotcomplete.style.display = "none";
		
		let lastMsg = document.createElement("p");
		lastMsg.textContent = 'Thanks for using our Chatbot!';
		lastMsg.classList.add('lastMessage');
		document.body.appendChild(lastMsg)
	}
}