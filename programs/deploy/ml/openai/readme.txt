Query
---
1. Get a copy of your API key and store in a file called 'openai_api_key.dat'
2. Compile code: obc -src openai_completion_0 -lib openai,net,json,misc
3. Run code: obr openai_completion_0 "What is the longest road in Boston?"

Assistant
---
1. Compile code: obc -src openai_assist_1 -lib openai,net,json,misc
2. Create and assistance and run query: obr openai_assist_1.obe create
3. Load assistance and run query: obr openai_assist_1 asst_xxxxxxxxxxxxxxxxxxxxxxxx