#pragma once

#include <windows.h>

class high_resolution_timer
{
public:
	high_resolution_timer()
	{
		LONGLONG counts_per_sec;
		QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&counts_per_sec));
		secondsPerCount = 1.0 / static_cast<double>(counts_per_sec);

		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&thisTime));
		baseTime = thisTime;
		lastTime = thisTime;
	}
	~high_resolution_timer() = default;
	high_resolution_timer(const high_resolution_timer&) = delete;
	high_resolution_timer& operator=(const high_resolution_timer&) = delete;
	high_resolution_timer(high_resolution_timer&&) noexcept = delete;
	high_resolution_timer& operator=(high_resolution_timer&&) noexcept = delete;

	// Returns the total time elapsed since Reset() was called, NOT counting any
	// time when the clock is stopped.
	float timeStamp() const  // in seconds
	{
		// If we are stopped, do not count the time that has passed since we stopped.
		// Moreover, if we previously already had a pause, the distance 
		// stop_time - base_time includes paused time, which we do not want to count.
		// To correct this, we can subtract the paused time from mStopTime:  
		//
		//                     |<--paused_time-->|
		// ----*---------------*-----------------*------------*------------*------> time
		//  base_time       stop_time        start_time     stop_time    this_time

		if (stopped)
		{
			return static_cast<float>(((stopTime - pausedTime) - baseTime) * secondsPerCount);
		}

		// The distance this_time - mBaseTime includes paused time,
		// which we do not want to count.  To correct this, we can subtract 
		// the paused time from this_time:  
		//
		//  (this_time - paused_time) - base_time 
		//
		//                     |<--paused_time-->|
		// ----*---------------*-----------------*------------*------> time
		//  base_time       stop_time        start_time     this_time
		else
		{
			return static_cast<float>(((thisTime - pausedTime) - baseTime) * secondsPerCount);
		}
	}

	float timeInterval() const  // in seconds
	{
		return static_cast<float>(deltaTime);
	}

	void reset() // Call before message loop.
	{
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&thisTime));
		baseTime = thisTime;
		lastTime = thisTime;

		stopTime = 0;
		stopped = false;
	}

	void start() // Call when unpaused.
	{
		LONGLONG start_time;
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&start_time));

		// Accumulate the time elapsed between stop and start pairs.
		//
		//                     |<-------d------->|
		// ----*---------------*-----------------*------------> time
		//  base_time       stop_time        start_time     
		if (stopped)
		{
			pausedTime += (start_time - stopTime);
			lastTime = start_time;
			stopTime = 0;
			stopped = false;
		}
	}

	void stop() // Call when paused.
	{
		if (!stopped)
		{
			QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&stopTime));
			stopped = true;
		}
	}

	void tick() // Call every frame.
	{
		if (stopped)
		{
			deltaTime = 0.0;
			return;
		}

		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&thisTime));
		// Time difference between this frame and the previous.
		deltaTime = (thisTime - lastTime) * secondsPerCount;

		// Prepare for next frame.
		lastTime = thisTime;

		// Force nonnegative.  The DXSDK's CDXUTTimer mentions that if the 
		// processor goes into a power save mode or we get shuffled to another
		// processor, then mDeltaTime can be negative.
		if (deltaTime < 0.0)
		{
			deltaTime = 0.0;
		}
	}

private:
	double secondsPerCount{ 0.0 };
	double deltaTime{ 0.0 };

	LONGLONG baseTime{ 0LL };
	LONGLONG pausedTime{ 0LL };
	LONGLONG stopTime{ 0LL };
	LONGLONG lastTime{ 0LL };
	LONGLONG thisTime{ 0LL };

	bool stopped{ false };
};
