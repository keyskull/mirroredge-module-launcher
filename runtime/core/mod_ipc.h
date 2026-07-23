#pragma once

namespace ModIpc {

void Start();
void Stop();
void Pump();
void ServicePump(DWORD timeoutMs);
bool IsListening();
bool WaitUntilListening(DWORD timeoutMs);

} // namespace ModIpc
