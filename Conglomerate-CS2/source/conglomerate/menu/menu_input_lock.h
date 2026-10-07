#pragma once

namespace MenuInputLock
{
	void suppressInput(void* input);
	void setBlocked(void* input, bool blocked);
	bool captureViewAngles(void* input, int slot);
	void restoreViewAngles();
}
