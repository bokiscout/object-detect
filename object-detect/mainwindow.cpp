#include "mainwindow.h"
#include "ui_mainwindow.h"


#include <QDebug>
#include <QMessageBox>


using namespace std;


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}


int MainWindow::loadImages(){
    // left image
    img_source = cv::imread("/home/borche/Documents/git/object-detect/images/books-opencv/box.png", cv::IMREAD_GRAYSCALE);
    if(img_source.empty()){
        string erormsg = "Left image (src) can not be loaded";
        cout << erormsg << endl;
        QMessageBox msg(this);
        msg.setText(erormsg.data());
        msg.exec();
        return -1;
    }

    // right image
    img_scene = cv::imread("/home/borche/Documents/git/object-detect/images/books-opencv/box_in_scene.png", cv::IMREAD_GRAYSCALE);
    if(img_scene.empty()){
        string errormsg = "Right image (scene) can not be loaded";
        cout << errormsg << endl;
        QMessageBox msg(this);
        msg.setText(errormsg.data());
        msg.exec();
        return -1;
    }

    return 1;
}


void MainWindow::on_pushButtonCpu_clicked()
{
    // first load iamges
    int result = loadImages();

    if(result > 0){
        executeOnCpu();
    }
}


void MainWindow::on_pushButtonGpu_clicked()
{
    // first load iamges
    int result = loadImages();

    if(result > 0){
        executeOnGpu();
    }
}


void MainWindow::executeOnCpu(){
    // write log in console
    cout << "Using serial (CPU) version" << endl;

    // create feature detecetor and feture descriptor
    cv::Ptr<cv::xfeatures2d::SURF> surf = cv::xfeatures2d::SURF::create(100);

    // Step 1: detect points of interest
    // Step 2: describe points of interest
    //
    // Note that steps 1 and 2 can be done separate or together
    surf->detectAndCompute(img_source, cv::noArray(), keypoints_source, descriptor_source, false);
    surf->detectAndCompute(img_scene, cv::noArray(), keypoints_scene, descriptor_scene, false);

    // print result
    cout << "Keypoints on source image: " << keypoints_source.size() << endl;
    cout << "Keypoints on scene image: " << keypoints_scene.size() << endl;

    cout << "Descripotr source: " << descriptor_source.size() << endl;
    cout << "Descripotr scene: " << descriptor_scene.size() << endl;
//    cout << "Descripotr source rows: " << descriptor_source.rows << endl;
//    cout << "Descripotr source cols: " << descriptor_source.cols << endl;

    // Step 3:
    // run matcher
    matcher.match(descriptor_source, descriptor_scene, matches);

    // print results
    cout << "Matches: " << matches.size() << endl;

    // Step 4:
    // calculate max and min distances between keypoints
    // use this to find best matches later
    for (int i = 0; i < descriptor_source.rows; i++)
    {
        double dist = matches[i].distance;
        if (dist < min_dist) min_dist = dist;
        if (dist > max_dist) max_dist = dist;
    }

    // print results
    cout << "Max dist: " << max_dist << endl;
    cout << "Min dist: " << min_dist << endl;

    // Step 5:
    // find best matches
    for (int i = 0; i < descriptor_source.rows; i++)
    {
        if (matches[i].distance <= max(2 * min_dist, 0.2))
        {
            good_matches.push_back(matches[i]);
        }
    }

    // print result
    cout << "Good matches: " << good_matches.size() << endl;

    // Step 6:
    // join sorce iamge and scene image into one large image
    // and draw lines between good matches
    cv::drawMatches(img_source, keypoints_source, img_scene,keypoints_scene,
                good_matches, img_matches, cv::Scalar::all(-1), cv::Scalar::all(-1),
                vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);

    // Step 7:
    // declare corners
    //
    // corners are used to draw square around detected (localised) object
    vector<cv::Point2f> source_corners(4);
    vector<cv::Point2f> scene_corners(4);

    // set corners acording to width of source image,
    // assume that source image is left, scene image is right
    source_corners[0] = (cvPoint(0,0));                                 // top left
    source_corners[1] = (cvPoint(img_source.cols,0));                   // top right
    source_corners[2] = (cvPoint(img_source.cols, img_source.rows));    // botom right
    source_corners[3] = (cvPoint(0,img_source.rows));                   // botom left

    // Step 8;
    // represent good_matches as vector of points
    // aka localize the object
    vector<cv::Point2f> obj;
    vector<cv::Point2f> scene;

    // get points of interest from good matrices
    if(good_matches.size() >= 4){
        for(int i=0; i<good_matches.size(); i++){
            obj.push_back(keypoints_source[good_matches[i].queryIdx].pt);
            scene.push_back(keypoints_scene[good_matches[i].trainIdx].pt);
        }
    }

    // Step 9:
    // find the transform between matched keypoints
    H = cv::findHomography(obj,scene, CV_RANSAC);

    // map pairs of the corresponding points
    cv::perspectiveTransform(source_corners, scene_corners, H);

    // draw square around detected object
    cv::line(img_matches, scene_corners[0] + cv::Point2f(img_source.cols,0), scene_corners[1] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);
    cv::line(img_matches, scene_corners[1] + cv::Point2f(img_source.cols,0), scene_corners[2] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);
    cv::line(img_matches, scene_corners[2] + cv::Point2f(img_source.cols,0), scene_corners[3] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);
    cv::line(img_matches, scene_corners[3] + cv::Point2f(img_source.cols,0), scene_corners[0] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);

    // Step 10:
    // display final image
    cv::imshow("Good matches", img_matches);

    // write two emtpy lines in log
    // this way it is beter for reading
    cout << "" << endl;
    cout << "" << endl;
}


void::MainWindow::executeOnGpu(){
    // check if cuda capable devices are found
    // if none is found then quit
    int cuda_devices_count = checkForCudaCapableDevice();
    if(cuda_devices_count <= 0){
        cout << "abort execution of parallel (GPU) version" << endl;
    }
    cout << "Using parallel (GPU) version" << endl;

    // Step 0 --extra:
    // upload images from host to device
    cv::cuda::GpuMat img_source_gpu;
    cv::cuda::GpuMat img_scene_gpu;

    img_source_gpu.upload(img_source);
    img_scene_gpu.upload(img_scene);

    // Step 1: detect points of interest
    // Step 2: describe points of interest
    //
    // Note that steps 1 and 2 can be done separate or together
    cv::cuda::GpuMat keypoints_source_gpu;
    cv::cuda::GpuMat keypoints_scene_gpu;

    cv::cuda::GpuMat descriptor_source_gpu;
    cv::cuda::GpuMat descriptor_scene_gpu;

    cv::cuda::SURF_CUDA surf_gpu(100);

    surf_gpu(img_source_gpu, cv::cuda::GpuMat(), keypoints_source_gpu, descriptor_source_gpu);
    surf_gpu(img_scene_gpu, cv::cuda::GpuMat(), keypoints_scene_gpu, descriptor_scene_gpu);

    // print result in console
    cout << "Keypoints on source image on GPU: " << keypoints_source_gpu.cols << endl;
    cout << "Keypoints on scene image on GPU: " << keypoints_scene_gpu.cols << endl;

    cout << "Descripotr source on GPU: " << descriptor_source_gpu.size() << endl;
    cout << "Descripotr scene on GPU: " << descriptor_scene_gpu.size() << endl;

    // Step 3:
    // run matcher
    cv::Ptr<cv::cuda::DescriptorMatcher> matcher_gpu = cv::cuda::DescriptorMatcher::createBFMatcher(surf_gpu.defaultNorm());
    vector<cv::DMatch> matches_gpu;

    matcher_gpu->match(descriptor_source_gpu, descriptor_scene_gpu, matches_gpu);

    // print results
    cout << "Matches: " << matches_gpu.size() << endl;

    // Step between 3 and 4 --extra
    // download from device to host
    vector<float> descriptor_source_downloaded;
    vector<float> descriptor_scene_downloaded;

    surf_gpu.downloadKeypoints(keypoints_source_gpu, keypoints_source);
    surf_gpu.downloadKeypoints(keypoints_scene_gpu, keypoints_scene);
//    surf_gpu.downloadDescriptors(descriptor_source_gpu, descriptor_source_downloaded);
//    surf_gpu.downloadDescriptors(descriptor_scene_gpu, descriptor_scene_downloaded);

    // alternate way to download descriptors from device to host
    descriptor_source = cv::Mat(descriptor_source_gpu);
    descriptor_scene = cv::Mat(descriptor_scene_gpu);

    // print result
    cout << ">|< Download from device to host >|<" << endl;

    cout << "Keypoints source on CPU: " << keypoints_source.size() << endl;
    cout << "Keypoints scene on CPU: " << keypoints_scene.size() << endl;

//    cout << "Descriptors source: " << descriptor_source_downloaded.size() << endl;
//    cout << "Descriptors scene: " << descriptor_scene_downloaded.size() << endl;

    cout << "Descriptor source CPU: " << descriptor_source.size() << endl;
    cout << "Descriptor scene CPU: " << descriptor_scene.size() << endl;

    // Step 4:
    // calculate max and min distances between keypoints
    // use this to find best matches later
    for (int i = 0; i < descriptor_source.rows; i++)
    {
        double dist = matches_gpu[i].distance;
        if (dist < min_dist) min_dist = dist;
        if (dist > max_dist) max_dist = dist;
    }

    // print results
    cout << "Max dist: " << max_dist << endl;
    cout << "Min dist: " << min_dist << endl;

    // Step 5:
    // find best matches
    for (int i = 0; i < descriptor_source.rows; i++)
    {
        if (matches_gpu[i].distance <= max(2 * min_dist, 0.2))
        {
            good_matches.push_back(matches_gpu[i]);
        }
    }

    // print result
    cout << "Good matches: " << good_matches.size() << endl;

    // Step 6:
    // join sorce iamge and scene image into one large image
    // and draw lines between good matches
    cv::drawMatches(img_source, keypoints_source, img_scene, keypoints_scene,
                good_matches, img_matches, cv::Scalar::all(-1), cv::Scalar::all(-1),
                vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);

    // Step 7:
    // declare corners
    //
    // corners are used to draw square around detected (localised) object
    vector<cv::Point2f> source_corners(4);
    vector<cv::Point2f> scene_corners(4);

    // set corners acording to width of source image,
    // assume that source image is left, scene image is right
    source_corners[0] = (cvPoint(0,0));                                 // top left
    source_corners[1] = (cvPoint(img_source.cols,0));                   // top right
    source_corners[2] = (cvPoint(img_source.cols, img_source.rows));    // botom right
    source_corners[3] = (cvPoint(0,img_source.rows));                   // botom left

    // Step 8;
    // represent good_matches as vector of points
    // aka localize the object
    vector<cv::Point2f> obj;
    vector<cv::Point2f> scene;

    // get points of interest from good matrices
    if(good_matches.size() >= 4){
        for(int i=0; i<good_matches.size(); i++){
            obj.push_back(keypoints_source[good_matches[i].queryIdx].pt);
            scene.push_back(keypoints_scene[good_matches[i].trainIdx].pt);
        }
    }

    // Step 9:
    // find the transform between matched keypoints
    H = cv::findHomography(obj,scene, CV_RANSAC);

    // map pairs of the corresponding points
    cv::perspectiveTransform(source_corners, scene_corners, H);

    // draw square around detected object
    cv::line(img_matches, scene_corners[0] + cv::Point2f(img_source.cols,0), scene_corners[1] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);
    cv::line(img_matches, scene_corners[1] + cv::Point2f(img_source.cols,0), scene_corners[2] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);
    cv::line(img_matches, scene_corners[2] + cv::Point2f(img_source.cols,0), scene_corners[3] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);
    cv::line(img_matches, scene_corners[3] + cv::Point2f(img_source.cols,0), scene_corners[0] + cv::Point2f(img_source.cols,0), cv::Scalar(0, 255, 0), 4);

    // Step 10:
    // display final image
    cv::imshow("Good matches GPU", img_matches);

    // Step X:
    // release gpu memory
    img_source_gpu.release();
    img_scene_gpu.release();

    keypoints_source_gpu.release();
    keypoints_scene_gpu.release();

    descriptor_source_gpu.release();
    descriptor_scene_gpu.release();

    surf_gpu.releaseMemory();

    matcher_gpu.release();

    // write two emtpy lines in log
    // this way it is beter for reading
    cout << "" << endl;
    cout << "" << endl;
}


int MainWindow::checkForCudaCapableDevice(){
    int devices = cv::cuda::getCudaEnabledDeviceCount();

    if(devices <= 0){
        QMessageBox message(this);
        cout << "No CUDA capable device found" << endl;
        message.setText("No CUDA capable device found");
        message.exec();
        return -1;
    }

    else{
       cv::cuda::printShortCudaDeviceInfo(devices);
       return devices;
    }
}


MainWindow::~MainWindow()
{
    delete ui;
}





