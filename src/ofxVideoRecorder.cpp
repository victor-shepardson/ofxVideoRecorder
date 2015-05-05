//
//  ofxVideoRecorder.cpp
//  ofxVideoRecorderExample
//
//  Created by Timothy Scaffidi on 9/23/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#include "ofxVideoRecorder.h"
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
#include <unistd.h>
#endif
#ifdef TARGET_WIN32
#include <io.h>
#endif
#include <fcntl.h>

int setNonblocking(int fd)
{
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	int flags;

	/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
	/* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	/* Otherwise, use the old way of doing it */
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
#endif
	return 0;
}

//===============================
execThread::execThread(){
	execCommand = "";
}

void execThread::setup(string command){
	execCommand = command;
	startThread(true, false);
}

void execThread::threadedFunction(){
	if (isThreadRunning()){
		system(execCommand.c_str());
	}
}

//===============================
ofxVideoDataWriterThread::ofxVideoDataWriterThread(){};
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
void ofxVideoDataWriterThread::setup(string filePath, lockFreeQueue<ofPixels *> * q){
	this->filePath = filePath;
	fd = -1;
	queue = q;
	bIsWriting = false;
	bClose = false;
	startThread(true, false);
}
#endif
#ifdef TARGET_WIN32
void ofxVideoDataWriterThread::setup(HANDLE videoHandle_, string filePath, lockFreeQueue<ofPixels *> * q){
	this->filePath = filePath;
	fd = -1;
	queue = q;
	bIsWriting = false;
	bClose = false;
	videoHandle = videoHandle_;
	startThread(true, false);
}
#endif
void ofxVideoDataWriterThread::threadedFunction(){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	if (fd == -1){
		fd = ::open(filePath.c_str(), O_WRONLY);
	}
#endif
	//maybe create file here? these threads act as the client and the main thread as the server?
	while (isThreadRunning())
	{
		ofPixels * frame = NULL;
		if (queue->Consume(frame) && frame){
			bIsWriting = true;
			int b_offset = 0;
			int b_remaining = frame->getWidth()*frame->getHeight()*frame->getBytesPerPixel();
			while (b_remaining > 0)
			{
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
				int b_written = ::write(fd, ((char *)frame->getPixels()) + b_offset, b_remaining);
#endif
#ifdef TARGET_WIN32
				DWORD b_written;
				if (!WriteFile(videoHandle, ((char *)frame->getPixels()) + b_offset, b_remaining, &b_written, 0)) {
					ofLogNotice("Video Thread") << "WriteFile to pipe failed. GLE " << GetLastError();
				}
#endif
				if (b_written > 0){
					b_remaining -= b_written;
					b_offset += b_written;
				}
				else {
					if (bClose){
						break; // quit writing so we can close the file
					}
				}
			}
			bIsWriting = false;
			frame->clear();
			delete frame;
		}
		else{
			condition.wait(conditionMutex, 1000);
			int test = 0;
		}
	}

#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	::close(fd); 
#endif
#ifdef TARGET_WIN32
	FlushFileBuffers(videoHandle);
	DisconnectNamedPipe(videoHandle);
	CloseHandle(videoHandle);
#endif
}

void ofxVideoDataWriterThread::signal(){
	condition.signal();
}

//===============================
ofxAudioDataWriterThread::ofxAudioDataWriterThread(){};
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
void ofxAudioDataWriterThread::setup(string filePath, lockFreeQueue<audioFrameShort *> *q){
	this->filePath = filePath;
	fd = -1;
	queue = q;
	bIsWriting = false;
	startThread(true, false);
}
#endif
#ifdef TARGET_WIN32
 void ofxAudioDataWriterThread::setup(HANDLE audioHandle_, string filePath, lockFreeQueue<audioFrameShort *> *q){
	this->filePath = filePath;
	fd = -1;
	queue = q;
	bIsWriting = false;
	audioHandle = audioHandle_;
	startThread(true, false);
}
#endif
void ofxAudioDataWriterThread::threadedFunction(){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )

	if (fd == -1){
		//write only, fd is the handle what is the windows eqivalent
		fd = ::open(filePath.c_str(), O_WRONLY);
	}
#endif
	while (isThreadRunning())
	{
		audioFrameShort * frame = NULL;
		if (queue->Consume(frame) && frame){
			bIsWriting = true;
			int b_offset = 0;
			int b_remaining = frame->size*sizeof(short);
			while (b_remaining > 0){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
				int b_written = ::write(fd, ((char *)frame->data) + b_offset, b_remaining);
#endif
#ifdef TARGET_WIN32
				DWORD b_written;
				if (!WriteFile(audioHandle, ((char *)frame->data) + b_offset, b_remaining, &b_written, 0)){
					ofLogNotice("Audio Thread") << "WriteFile to pipe failed. GLE " << GetLastError();
				}
#endif
				if (b_written > 0){
					b_remaining -= b_written;
					b_offset += b_written;
				}
				else {
					if (bClose){
						break; // quit writing so we can close the file
					}
				}
			}
			bIsWriting = false;
			delete[] frame->data;
			delete frame;
		}
		else{
			condition.wait(conditionMutex, 1000);
		}
	}

#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	::close(fd);
#endif
#ifdef TARGET_WIN32
	FlushFileBuffers(audioHandle);
	DisconnectNamedPipe(audioHandle);
	CloseHandle(audioHandle);
#endif
}
void ofxAudioDataWriterThread::signal(){
	condition.signal();
}

//===============================
ofxVideoRecorder::ofxVideoRecorder()
{
	bIsInitialized = false;
	ffmpegLocation = "ffmpeg";
	videoCodec = "mpeg4";
	audioCodec = "pcm_s16le";
	videoBitrate = "2000k";
	audioBitrate = "128k";
	pixelFormat = "rgb24";
}

bool ofxVideoRecorder::setup(string fname, int w, int h, float fps, int sampleRate, int channels)
{
	if (bIsInitialized)
	{
		close();
	}

	fileName = fname;
	string absFilePath = ofFilePath::getAbsolutePath(fileName);

	moviePath = ofFilePath::getAbsolutePath(fileName);

	stringstream outputSettings;
	outputSettings
		<< " -vcodec " << videoCodec
		<< " -b " << videoBitrate
		<< " -acodec " << audioCodec
		<< " -ab " << audioBitrate
		<< " " << absFilePath;

	return setupCustomOutput(w, h, fps, sampleRate, channels, outputSettings.str());
}

bool ofxVideoRecorder::setupCustomOutput(int w, int h, float fps, string outputString){
	return setupCustomOutput(w, h, fps, 0, 0, outputString);
}

bool ofxVideoRecorder::setupCustomOutput(int w, int h, float fps, int sampleRate, int channels, string outputString)
{
	if (bIsInitialized)
	{
		close();
	}

	bRecordAudio = (sampleRate > 0 && channels > 0);
	bRecordVideo = (w > 0 && h > 0 && fps > 0);
	bFinishing = false;

	videoFramesRecorded = 0;
	audioSamplesRecorded = 0;

	if (!bRecordVideo && !bRecordAudio) {
		ofLogWarning() << "ofxVideoRecorder::setupCustomOutput(): invalid parameters, could not setup video or audio stream.\n"
			<< "video: " << w << "x" << h << "@" << fps << "fps\n"
			<< "audio: " << "channels: " << channels << " @ " << sampleRate << "Hz\n";
		return false;
	}
	videoPipePath = "";
	audioPipePath = "";
	pipeNumber = requestPipeNumber();
	if (bRecordVideo) {
		width = w;
		height = h;
		frameRate = fps;

		// recording video, create a FIFO pipe
		videoPipePath = ofFilePath::getAbsolutePath("ofxvrpipe" + ofToString(pipeNumber));
		if (!ofFile::doesFileExist(videoPipePath)){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
			string cmd = "bash --login -c 'mkfifo " + videoPipePath + "'";
			system(cmd.c_str());
			// TODO: add windows compatable pipe creation (does ffmpeg work with windows pipes?)

#endif
#ifdef TARGET_WIN32
			//ofFile(videoPipePath, ofFile::ReadWrite);

			vPipename = TEXT("\\\\.\\pipe\\videoPipe");

			hVPipe = CreateNamedPipe(
				vPipename, // name of the pipe
				PIPE_ACCESS_OUTBOUND, // 1-way pipe -- send only
				PIPE_TYPE_BYTE, // send data as a byte stream
				1, // only allow 1 instance of this pipe
				0, // no outbound buffer
				0, // no inbound buffer
				0, // use default wait time
				NULL // use default security attributes
				);

			if (!(hVPipe != INVALID_HANDLE_VALUE)){
				// Exit if an error other than ERROR_PIPE_BUSY occurs. 

				if (GetLastError() != ERROR_PIPE_BUSY)
				{
					ofLogError("Video Recorder Pipe") << "Could not open audio pipe.";
				}

				// All pipe instances are busy, so wait for 20 seconds. 

				if (!WaitNamedPipe(vPipename, 20000))
				{
					ofLogError("Video Recorder Pipe") << "Could not open pipe: 20 second wait timed out.";
				}
			}

#endif
		}
	}

	if (bRecordAudio) {
		this->sampleRate = sampleRate;
		audioChannels = channels;

		// recording video, create a FIFO pipe
		audioPipePath = ofFilePath::getAbsolutePath("ofxarpipe" + ofToString(pipeNumber));
		if (!ofFile::doesFileExist(audioPipePath)){
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
			string cmd = "bash --login -c 'mkfifo " + audioPipePath + "'";
			system(cmd.c_str());
#endif
			// TODO: add windows compatable pipe creation (does ffmpeg work with windows pipes?)
#ifdef TARGET_WIN32
			//ofFile(audioPipePath, ofFile::ReadWrite);
			aPipename = TEXT("\\\\.\\pipe\\audioPipe");
			
			hAPipe = CreateNamedPipe(
				aPipename,
				PIPE_ACCESS_OUTBOUND, // 1-way pipe -- send only
				PIPE_TYPE_BYTE, // send data as a byte stream
				1, // only allow 1 instance of this pipe
				0, // no outbound buffer
				0, // no inbound buffer
				0, // use default wait time
				NULL // use default security attributes
				);

			if (!(hAPipe != INVALID_HANDLE_VALUE)){
				// Exit if an error other than ERROR_PIPE_BUSY occurs. 

				if (GetLastError() != ERROR_PIPE_BUSY)
				{
					ofLogError("Video Recorder Pipe") << "Could not open audio pipe.";
				}

				// All pipe instances are busy, so wait for 20 seconds. 

				if (!WaitNamedPipe(aPipename, 20000))
				{
					ofLogError("Video Recorder Pipe") << "Could not open pipe: 20 second wait timed out.";
				}
			}

#endif
		}
	}

	stringstream cmd;
	// basic ffmpeg invocation, -y option overwrites output file
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )
	cmd << "bash --login -c '" << ffmpegLocation << " -y";

	if (bRecordAudio){
		cmd << " -acodec pcm_s16le -f s16le -ar " << sampleRate << " -ac " << audioChannels << " -i " << audioPipePath;
	}
	else { // no audio stream
		cmd << " -an";
	}
	if (bRecordVideo){ // video input options and file
		cmd << " -r " << fps << " -s " << w << "x" << h << " -f rawvideo -pix_fmt " << pixelFormat << " -i " << videoPipePath << " -r " << fps;
	}
	else { // no video stream
		cmd << " -vn";
	}
	cmd << " " + outputString + "' &";

	//cerr << cmd.str();
#endif
#ifdef TARGET_WIN32
	cmd << ffmpegLocation << "ffmpeg" << " -y ";

	if (bRecordAudio){
		cmd << " -acodec pcm_s16le -f s16le -ar " << sampleRate << " -ac " << audioChannels << " -i " << "\\\\.\\pipe\\audioPipe";
	}
	else { // no audio stream
		cmd << " -an";
	}
	if (bRecordVideo){ // video input options and file
		cmd << " -r " << fps << " -s " << w << "x" << h << " -f rawvideo -pix_fmt " << pixelFormat << " -i " << "\\\\.\\pipe\\videoPipe";
	}
	else { // no video stream
		cmd << " -vn";
	}
	cmd << " " << outputString;

	//cerr << cmd.str();
#endif


	cout << "Command " << cmd.str() << endl;
	ffmpegThread.setup(cmd.str()); // start ffmpeg thread, will wait for input pipes to be opened

	if (bRecordAudio){
		//        audioPipeFd = ::open(audioPipePath.c_str(), O_WRONLY);
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )

		audioThread.setup(audioPipePath, &audioFrames);
#endif
#ifdef TARGET_WIN32
		//this blocks, so we have to call it after ffmpeg is listening for a pipe?
		bool fSuccess = ConnectNamedPipe(hAPipe, NULL);
		if (!fSuccess)
		{
			ofLogNotice("Audio Pipe") << "SetNamedPipeHandleState failed. GLE " << GetLastError();
		}
		else {
			cout << "\n==========================\nAudio Pipe Connected Successfully\n==========================\n" << endl;
		}
		//audioThread.setup(hAFPipe, hAPipe, ofToString(aPipename), &audioFrames);
#endif
	}
	if (bRecordVideo){
		//        videoPipeFd = ::open(videoPipePath.c_str(), O_WRONLY);
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )

		videoThread.setup(videoPipePath, &frames);
#endif
#ifdef TARGET_WIN32
		//this blocks, so we have to call it after ffmpeg is listening for a pipe?
		cout << "connect video pipe" << endl;
		bool fSuccess = ConnectNamedPipe(hVPipe, NULL);
		if (!fSuccess)
		{
			ofLogNotice("Video Pipe") << "SetNamedPipeHandleState failed. GLE " << GetLastError();
		}
		else {
			cout << "\n==========================\nVideo Pipe Connected Successfully\n==========================\n" << endl;
		}
		//videoThread.setup(hVFPipe, hVPipe, ofToString(vPipename), &frames);
#endif
	}
	if (bRecordAudio) audioThread.setup(hAPipe, ofToString(aPipename), &audioFrames);
	if (bRecordVideo) videoThread.setup(hVPipe, ofToString(vPipename), &frames);
	bIsInitialized = true;

	return bIsInitialized;
}

void ofxVideoRecorder::addFrame(const ofPixels &pixels)
{
	if (bIsInitialized && bRecordVideo)
	{
		int framesToAdd = 1; //default add one frame per request
		if (bRecordAudio && !bFinishing){
			//if also recording audio, check the overall recorded time for audio and video to make sure audio is not going out of sync
			//this also handles incoming dynamic framerate while maintaining desired outgoing framerate
			double videoRecordedTime = videoFramesRecorded / frameRate;
			double audioRecordedTime = (audioSamplesRecorded / audioChannels) / (double)sampleRate;
			double avDelta = audioRecordedTime - videoRecordedTime;

			if (avDelta > 1.0 / frameRate) {
				//more than one video frame's worth of audio data is waiting, we need to send extra video frames.
				int numFramesCopied = 0;
				while (avDelta > 1.0 / frameRate) {
					framesToAdd++;
					avDelta -= 1.0 / frameRate;
				}
				ofLogVerbose() << "ofxVideoRecorder: avDelta = " << avDelta << ". Not enough video frames for desired frame rate, copied this frame " << framesToAdd << " times.\n";
			}
			else if (avDelta < -1.0 / frameRate){
				//more than one video frame is waiting, skip this frame
				framesToAdd = 0;
				ofLogVerbose() << "ofxVideoRecorder: avDelta = " << avDelta << ". Too many video frames, skipping.\n";
			}
		}
		for (int i = 0; i < framesToAdd; i++){
			//add desired number of frames
			frames.Produce(new ofPixels(pixels));
			videoFramesRecorded++;
		}

		videoThread.signal();
	}
}

void ofxVideoRecorder::addAudioSamples(float *samples, int bufferSize, int numChannels){
	if (bIsInitialized && bRecordAudio){
		int size = bufferSize*numChannels;
		audioFrameShort * shortSamples = new audioFrameShort;
		shortSamples->data = new short[size];
		shortSamples->size = size;

		for (int i = 0; i < size; i++){
			shortSamples->data[i] = (short)(samples[i] * 32767.0f);
		}
		audioFrames.Produce(shortSamples);
		audioThread.signal();
		audioSamplesRecorded += size;
	}
}

void ofxVideoRecorder::close()
{
	if (!bIsInitialized) return;
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )

	//set pipes to non_blocking so we dont get stuck at the final writes
	setNonblocking(audioPipeFd);
	setNonblocking(videoPipeFd);

	//gettting stuck here... is it not clearing the buffer?
	while (frames.size() > 0 && audioFrames.size() > 0) {
		// if there are frames in the queue or the thread is writing, signal them until the work is done.
		videoThread.signal();
		audioThread.signal();
	}
#endif
	//at this point all data that ffmpeg wants should have been consumed
	// one of the threads may still be trying to write a frame,
	// but once close() gets called they will exit the non_blocking write loop
	// and hopefully close successfully

	bIsInitialized = false;


	if (bRecordVideo) {
		videoThread.close();
	}
	if (bRecordAudio) {
		audioThread.close();
	}
#if defined( TARGET_OSX ) || defined( TARGET_LINUX )

	retirePipeNumber(pipeNumber);
#endif
	ffmpegThread.waitForThread();
	// TODO: kill ffmpeg process if its taking too long to close for whatever reason.
	ofLogNotice("ofxVideoRecorder") << "Closed ffmpeg";

}

set<int> ofxVideoRecorder::openPipes;

int ofxVideoRecorder::requestPipeNumber(){
	int n = 0;
	while (openPipes.find(n) != openPipes.end()) {
		n++;
	}
	openPipes.insert(n);
	return n;
}

void ofxVideoRecorder::retirePipeNumber(int num){
	if (!openPipes.erase(num)){
		ofLogNotice() << "ofxVideoRecorder::retirePipeNumber(): trying to retire a pipe number that is not being tracked: " << num << endl;
	}
}
