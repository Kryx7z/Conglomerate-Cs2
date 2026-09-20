#pragma once

namespace Movement
{
	void suppressInput(void* input);
	void setInputBlocked(void* input, bool blocked);
	bool captureViewAngles(void* input, int slot);
	void restoreViewAngles();
}
