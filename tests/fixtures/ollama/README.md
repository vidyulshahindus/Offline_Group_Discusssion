# Ollama `/api/chat` fixtures

Manual comparison (model must be loaded in Ollama):

```powershell
curl.exe -s -N -H "Content-Type: application/json" `
  --data-binary "@tests/fixtures/ollama/curl-chat-hi-no-think.json" `
  http://127.0.0.1:11434/api/chat

curl.exe -s -N -H "Content-Type: application/json" `
  --data-binary "@tests/fixtures/ollama/curl-chat-hi-think-false.json" `
  http://127.0.0.1:11434/api/chat
```

Without `think:false`, Qwen 3.5 / `eagle-clm` often streams only `message.thinking` with empty `content`.
