#pragma once

class CLightDataQueue
{
public:
	char pad_0000[24]; // 0x0000 - 0x0017
	void* light_data;  // 0x0018
};

class ISceneSystem
{
public:
	char pad_0000[0x1EB8];              // 0x0000 - 0x1EB7
	CLightDataQueue* light_data_queue;  // 0x1EB8
};
