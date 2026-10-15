# Security

## Do not commit or publish

- API keys, tokens, passwords (`.env`, `**/key.json`, `**/credentials.json`)
- Student transcripts, chat logs, or audio under `logs/`, `generated_speech/`, `tts_cache/`
- Private reference trees such as `New-discussion-eleven/` (listed in `.gitignore`)
- Large local tool binaries under `tools/` (whisper models, Piper voices)

## Application defaults

- LLM traffic is intended for **local Ollama** (`llm.base_url` in config). Do not point production school builds at untrusted endpoints without review.
- Logs should avoid user content; prefer ids, counts, and timings (see project security rules in the build spec).

## Reporting

For this personal/school project, report issues to the repository owner via GitHub Issues (if enabled) or direct contact with the maintainer.
