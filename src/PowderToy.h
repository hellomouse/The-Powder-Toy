#pragma once

// void EngineProcess();
extern "C" {
    void Shutdown();
}
void EmscriptenShutdown();
void ClipboardPush(ByteString text);
ByteString ClipboardPull();
int GetModifiers();
unsigned int GetTicks();
