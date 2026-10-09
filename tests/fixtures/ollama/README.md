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

## Prefix-cache bench (3 turns)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/fixtures/ollama/bench-cache-turn.ps1 -Model eagle-clm
powershell -NoProfile -ExecutionPolicy Bypass -File tests/fixtures/ollama/bench-cache-turn.ps1 -Model llama3.2:3b
```

Read **`prompt_eval_new`** (= `prompt_eval_count` − `prompt_eval_cached_count`). That should track server.log “prompt eval … / N tokens” on turns 2–3. Compare with F12 after rebuilding EagleGD.
