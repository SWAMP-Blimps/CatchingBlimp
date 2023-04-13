// ============================== INCLUDES ==============================
#include <libv4l2.h>
#include <iostream>

#include "CameraHandler.h"
#include "PiComm.h"

using namespace std;
using namespace cv;

// ============================== CLASS ==============================

// Store pointer to piComm, open camera
void CameraHandler::init(PiComm* piComm, ProgramData* programData){
    this->piComm = piComm;
    this->programData = programData;

    // Check that program is still running
	if(!programData->program_running){
		fprintf(stdout, "CameraHandler initialized with program_running=false. Stopping.\n");
		return;
	}

    // Set up video capture
	cv::VideoCapture cap;

    int capID;
    if(programData->setCaptureID){
        capID = programData->customCaptureID;
        cap.open(capID, CAMERA_API);
        if(!cap.isOpened()){
			fprintf(stderr, "Unable to open camera (index=%d).\n", capID);
            programData->program_running = false;
            return;
        }
    }else{
        capID = CAMERA_INDEX;
        cap.open(capID, CAMERA_API);
        if(!cap.isOpened()){
			fprintf(stderr, "Unable to open camera (index=%d).\n", capID);
            
            capID = CAMERA_INDEX_BACKUP;
            cap.open(capID, CAMERA_API);
            if(!cap.isOpened()){
                fprintf(stderr, "Unable to open camera (index=%d).\n", capID);
                programData->program_running = false;
                return;
            }
        }
    }
	fprintf(stdout, "Successfully opened camera (index=%d).\n", capID);

    // Set the stereo cam to full resolution
	cap.set(cv::CAP_PROP_FRAME_WIDTH, CAMERA_WIDTH);
	cap.set(cv::CAP_PROP_FRAME_HEIGHT, CAMERA_HEIGHT);

    // Init class data
    newFrameNum = 0;
    prevFrameNum = 0;

    // Start thread
    if (pthread_create(&capture_thread, NULL, CameraHandler::staticCaptureThread_start, this) < 0) {
        perror("pthread_create failure in CameraHandler");
        exit(EXIT_FAILURE);
    }
}

// Static function to run thread member function
// https://cplusplus.com/forum/unices/138864/
void* CameraHandler::staticCaptureThread_start(void* arg){
    static_cast<CameraHandler*>(arg)->captureThread_loop();
}

void CameraHandler::captureThread_loop(){
    while(programData->program_running){
        // If no frame is available, continue
        if(!cap.grab()) continue;
        
        Mat rawFrame;
        cap.retrieve(rawFrame);

        // Crop the left and right images
        Rect left_roi(0, 0, rawFrame.cols/2, rawFrame.rows);
        Rect right_roi(rawFrame.cols/2, 0, rawFrame.cols/2, rawFrame.rows);
        Mat leftFrame_maxres(rawFrame, left_roi);
        Mat rightFrame_maxres(rawFrame, right_roi);

        // Reduce image size for rectification
        Mat leftFrame_lowres, rightFrame_lowres;
        resize(leftFrame_maxres, leftFrame_lowres, Size(RECT_WIDTH, RECT_HEIGHT), INTER_LINEAR);
        resize(rightFrame_maxres, rightFrame_lowres, Size(RECT_WIDTH, RECT_HEIGHT), INTER_LINEAR);

        // Copy images to shared memory
        pthread_mutex_lock(&mutex_newFrame);
        leftFrameCopied = leftFrame_lowres;
        rightFrameCopied = rightFrame_lowres;
        pthread_mutex_unlock(&mutex_newFrame);
        pthread_mutex_lock(&mutex_newFrameNum);
        newFrameNum++;
        pthread_mutex_unlock(&mutex_newFrameNum);

        if(!programData->annotatedMode){
            piComm->setStreamFrame(leftFrame_lowres);
        }
    }
}

bool CameraHandler::getRecentFrames(Mat* leftFrame, Mat* rightFrame){
    // Really fast mutex
    unsigned int tempFrameNum;
    pthread_mutex_lock(&mutex_newFrameNum);
    tempFrameNum = newFrameNum;
    pthread_mutex_unlock(&mutex_newFrameNum);

    if(prevFrameNum > tempFrameNum){
        // Current frame has not been acquired, get and return it
        prevFrameNum = tempFrameNum;
        Mat leftFrameTemp, rightFrameTemp;
        pthread_mutex_lock(&mutex_newFrame);
        leftFrameTemp = leftFrameCopied;
        rightFrameTemp = rightFrameCopied;
        pthread_mutex_unlock(&mutex_newFrame);
        // Return frames and true
        *leftFrame = leftFrameTemp;
        *rightFrame = rightFrameTemp;
        return true;
    }else{
        // Current frame has already been got, do not return it
        return false;
    }
}