Websocket support, improved stability, and bug fixes

v2025.2.1
• Improved support for JSON stream parsing ⭐
• Faster 'String' compare 🚄

v2025.2.0
• Add WebSocket support (done)
• Performance testing for Llama and Mistra CPU execution 🧮 (done)
• Improve SDL2 stability for Windows on arm64 🎮 (done)
• Bug fixes #509 and #510 🐛

v2025.1.1
• Support for Windows on arm64🦾
• Windows OpenSSL libraries upgraded to 3.4.0🔒
• UDP socket support🛜
• Fixes bugs🪲
	○ Resolved a compiler bug with linking functions in libraries that returned an array of generic objects
	○ Fixed SDL2 call to 'GetGlobalMouseState(..)'
	○ Settled a Gemini issue that expected 'safetySettings' to always be returned in calls to 'GetContent(..)'
• Fixed broken code examples🩹