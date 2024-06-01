Query
---
1. Store your API key in a file 'gemini_api_key.dat' or call 'EndPoint->SetApiKey()' to specify a specific file location
2. Compile code: obc -src gemini_query_0 -lib gemini,json,encrypt,net,misc
3. Run code: obr gemini_query_0.obe "What is the oldest hotel in the US?. Output the answer in JSON"

Assistant
---
1. Create and OAuth access file, named 'client_secrets.json' see: https://developers.google.com/api-client-library/dotnet/guide/aaa_client_secrets
2. Compile code: obc -src gemini_assist_1 -lib gemini,json,encrypt,net,misc
3. Create assistant: obr gemini_assist_1 create_tuned_model
3. Query assistant: obr gemini_assist_1 query_tuned_model "tunedModels/number-generator-model-xxxxxxxxxxxx" 14
