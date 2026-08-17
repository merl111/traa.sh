# Testing traa.sh

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Fixtures

- `fixtures/vt/*.in` — VT byte streams
- `golden/*.screen` — expected dumps (update with `TRAASH_UPDATE_GOLDEN=1` when tooling supports it)

## Suites

- C unit: utf8, ring, VT, mux, ipc, keymap
- Headless CLI smoke / demo auto
- Lua: themes, status styles, plugins, demo steps
