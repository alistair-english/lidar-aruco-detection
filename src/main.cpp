#include <cstdlib>
#include <ctime>
#include <float.h>
#include <iostream>
#include <queue>
#include <string>
#include <vector>
#include <vtkAutoInit.h>

#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

#include <opencv2/aruco.hpp>
#include <pcl/ModelCoefficients.h>
#include <pcl/common/common.h>
#include <pcl/common/common_headers.h>
#include <pcl/common/transforms.h>
#include <pcl/console/parse.h>
#include <pcl/features/intensity_gradient.h>
#include <pcl/features/moment_of_inertia_estimation.h>
#include <pcl/features/normal_3d.h>
#include <pcl/filters/crop_box.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/io/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/io/png_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/range_image/impl/range_image.hpp>
#include <pcl/range_image/range_image.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/visualization/common/float_image_utils.h>
#include <pcl/visualization/pcl_visualizer.h>

#include <Eigen/Core>

#include "configuration.hpp"
#include "visualisation.hpp"

using namespace cv;
using namespace std;
using namespace pcl;

int *rand_rgb() {
    int *rgb = new int[3];
    rgb[0] = rand() % 255;
    rgb[1] = rand() % 255;
    rgb[2] = rand() % 255;

    return rgb;
}

std::vector<std::vector<float>> pts1;
vector<int> id_num;
bool use_flip = false;

int main(int argc, char **argv) {

    const Configuration config = load_configuration("config.yaml");

    pcl::PointCloud<pcl::PointXYZI>::Ptr cloud = load_pointcloud(config.filename);
    if (!cloud) {
        return 0;
    }

    auto [viewer, viewports] = create_visualizer();
    add_raw_pointcloud(viewer, cloud, viewports.v1);

    int j = 0;
    int jj = 0;

    // --- extract features ---
    // Estimate the surface normals
    pcl::PointCloud<pcl::Normal>::Ptr cloud_n(new pcl::PointCloud<pcl::Normal>);
    pcl::NormalEstimation<pcl::PointXYZI, pcl::Normal> norm_est;
    // norm_est.setInputCloud(ext_cloud_ptr);
    norm_est.setInputCloud(cloud);
    norm_est.setInputCloud(cloud);

    pcl::search::KdTree<pcl::PointXYZI>::Ptr treept1(new pcl::search::KdTree<pcl::PointXYZI>(false));
    norm_est.setSearchMethod(treept1);
    norm_est.setRadiusSearch(config.norm_radius); // search threshold 0.005

    norm_est.compute(*cloud_n);

    // Estimate the Intensity Gradient
    pcl::PointCloud<pcl::IntensityGradient>::Ptr cloud_ig(new pcl::PointCloud<pcl::IntensityGradient>);
    PointCloud<IntensityGradient> gradient;
    pcl::IntensityGradientEstimation<pcl::PointXYZI, pcl::Normal, pcl::IntensityGradient> gradient_est;

    gradient_est.setInputCloud(cloud);
    gradient_est.setInputNormals(cloud_n);

    pcl::search::KdTree<pcl::PointXYZI>::Ptr treept2(new pcl::search::KdTree<pcl::PointXYZI>(false));
    gradient_est.setSearchMethod(treept2);
    gradient_est.setRadiusSearch(config.gradient_radius); // search threshold 0.005
    gradient_est.compute(gradient);

    // float intensity_threshold = 5000; //intensity threshold 2000
    float intensity_threshold = config.intensity_threshold;
    vector<std::vector<float>> valid_points;
    vector<float> valid_point;
    int counter = 0;
    float intensity_max = 0.001;
    float mid;

    priority_queue<float> q;
    float ratio_q;

    ratio_q = 0.02;

    if (cloud->size() & 1) {
        //   int len = (cloud->size()+1)/2;
        int len = cloud->size();

        for (int i = 0; i < len; ++i) {
            const float *g_est = gradient[i].gradient;
            float magnitude = std::sqrt(g_est[0] * g_est[0] + g_est[1] * g_est[1] + g_est[2] * g_est[2]);
            if (!isnan(magnitude))
                q.push(magnitude);
        }

        /*for(int i=len;i<cloud->size();++i)
        {
              const float * g_est = gradient[i].gradient;
              float magnitude =
        std::sqrt(g_est[0]*g_est[0]+g_est[1]*g_est[1]+g_est[2]*g_est[2]); if
        (q.top()<magnitude)
              {
                      q.pop();
                      q.push(magnitude);
              }


        }*/

        for (int i = 0; i < q.size() * ratio_q; i++) {
            q.pop();
        }

        mid = q.top();
    }

    if (cloud->size() & 2) {
        // int len = (cloud->size())/2;

        int len = cloud->size();
        for (int i = 0; i < len; ++i) {
            const float *g_est = gradient[i].gradient;
            float magnitude = std::sqrt(g_est[0] * g_est[0] + g_est[1] * g_est[1] + g_est[2] * g_est[2]);
            if (!isnan(magnitude))
                q.push(magnitude);
        }
        /*int flag =0;
                for(int i=len;i<cloud->size();++i)
        {
              const float * g_est = gradient[i].gradient;
              float magnitude =
        std::sqrt(g_est[0]*g_est[0]+g_est[1]*g_est[1]+g_est[2]*g_est[2]); if
        (q.top()<magnitude)
              {
                      flag = q.top();
                      q.pop();
                      q.push(magnitude);
              }


        }*/

        // mid = (q.top()+flag)/2;
        for (int i = 0; i < q.size() * ratio_q; i++) {
            q.pop();
        }

        mid = q.top();
    }

    // cout<<"mid"<<mid<<endl;

    intensity_threshold = mid;

    for (size_t p = 0; p < cloud->size(); ++p) {

        const float *g_est = gradient[p].gradient;
        float magnitude = std::sqrt(g_est[0] * g_est[0] + g_est[1] * g_est[1] + g_est[2] * g_est[2]);
        // if(magnitude>intensity_max)
        if (!isnan(magnitude))
            // intensity_max +=  magnitude;
            // magnitude = magnitude/intensity_max ;
            // if (!isnan(magnitude) && magnitude > threshold){
            if (magnitude > intensity_threshold) {
                ++counter;
                valid_point = {cloud->points[p].x, cloud->points[p].y, cloud->points[p].z, cloud->points[p].intensity};
                valid_points.push_back(valid_point);
            } // for loop to check magnitude
    }

    // cout<<"mean"<<intensity_max/cloud->size()<<endl;

    pcl::PointCloud<pcl::PointXYZI>::Ptr intensity_feature(new pcl::PointCloud<pcl::PointXYZI>);
    pcl::PointCloud<pcl::PointXYZI> &intensity_feature_pointcloud = *intensity_feature;

    intensity_feature_pointcloud.width = counter;
    intensity_feature_pointcloud.height = 1;
    intensity_feature_pointcloud.is_dense = false;
    intensity_feature_pointcloud.points.resize(
        intensity_feature_pointcloud.width * intensity_feature_pointcloud.height
    );

    for (size_t q = 0; q < counter; ++q) {
        intensity_feature_pointcloud.points[q].x = valid_points[q][0];
        intensity_feature_pointcloud.points[q].y = valid_points[q][1];
        intensity_feature_pointcloud.points[q].z = valid_points[q][2];
    }
    add_feature_pointcloud(viewer, intensity_feature, viewports);

    // 		//-------------------------------------features
    // extracted----------------------------------------------------
    // 		// Conduct EuclideanCluster on the features
    pcl::search::KdTree<pcl::PointXYZI>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZI>);
    tree->setInputCloud(intensity_feature);
    std::vector<pcl::PointIndices> cluster_indices;

    pcl::EuclideanClusterExtraction<pcl::PointXYZI> ec;
    ec.setClusterTolerance(config.EuclideanTolerance); // search tolerance fuck it! 0.01
    ec.setMinClusterSize(100);
    ec.setMaxClusterSize(100000);
    ec.setSearchMethod(tree);
    ec.setInputCloud(intensity_feature);
    ec.extract(cluster_indices);

    // // 			//pcl::PCDWriter writer;

    std::vector<float> moment_of_inertia;
    std::vector<float> eccentricity;
    pcl::PointXYZI min_point_OBB;
    pcl::PointXYZI max_point_OBB;
    pcl::PointXYZI position_OBB;
    Eigen::Matrix3f rotational_matrix_OBB;
    float major_value, middle_value, minor_value;
    Eigen::Vector3f major_vector, middle_vector, minor_vector;
    Eigen::Vector3f mass_center;

    for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin(); it != cluster_indices.end();
         ++it) {
        pcl::PointCloud<pcl::PointXYZI>::Ptr cluster(new pcl::PointCloud<pcl::PointXYZI>);
        // cluster_counter++;
        // std::cout<<"the number of the cluster is"<<""<<cluster_counter<<'\n';
        for (const auto &idx : it->indices)
            cluster->push_back((*intensity_feature)[idx]);

        cluster->width = cluster->size();
        cluster->height = 1;
        cluster->is_dense = true;

        pcl::MomentOfInertiaEstimation<pcl::PointXYZI> feature_extractor;
        feature_extractor.setInputCloud(cluster);
        feature_extractor.compute();

        feature_extractor.getMomentOfInertia(moment_of_inertia);
        feature_extractor.getEccentricity(eccentricity);
        feature_extractor.getOBB(min_point_OBB, max_point_OBB, position_OBB, rotational_matrix_OBB);
        feature_extractor.getEigenValues(major_value, middle_value, minor_value);
        feature_extractor.getEigenVectors(major_vector, middle_vector, minor_vector);
        feature_extractor.getMassCenter(mass_center);

        Eigen::Vector3f position(position_OBB.x, position_OBB.y, position_OBB.z);
        Eigen::Quaternionf quat(rotational_matrix_OBB);

        Eigen::Vector3f euler_angles = rotational_matrix_OBB.eulerAngles(1, 0, 2);

        float OBB_width = max_point_OBB.x - min_point_OBB.x;
        float OBB_height = max_point_OBB.y - min_point_OBB.y;
        float OBB_depth = max_point_OBB.z - min_point_OBB.z;
        float max1, max2;
        float OBB_dia = sqrt(OBB_width * OBB_width + OBB_height * OBB_height + OBB_depth * OBB_depth);

        max1 = max2 = OBB_width;
        if (OBB_height > max1)
            max1 = OBB_height;
        else {
            max2 = OBB_height;
        }

        if (OBB_depth > max1) {
            max2 = max1;
            max1 = OBB_depth;
        } else if (OBB_depth < max1 && OBB_depth >= max2)
            max2 = OBB_depth;
        // test
        add_oriented_bounding_box(
            viewer,
            position,
            quat,
            OBB_width,
            OBB_height,
            OBB_depth,
            "OBBoriginal" + std::to_string(2 * jj),
            viewports.v3
        );

        jj++;

        if (max1 / max2 < config.ratio_threshold && max2 / max1 > 1 / config.ratio_threshold &&
            config.cuboid_dia_min <= OBB_dia && OBB_dia <= config.cuboid_dia_max) {
            // if (max1/max2<config.ratio_threshold && max2/max1>1/config.ratio_threshold){
            // if (max1/max2<config.ratio_threshold && max2/max1>1/config.ratio_threshold &&
            // config.cuboid_area_min <= (max1*max2) && (max1*max2)<=config.cuboid_area_max){

            pcl::PointCloud<pcl::PointXYZI>::Ptr points_in_box{new pcl::PointCloud<pcl::PointXYZI>};
            pcl::PointCloud<pcl::PointXYZI>::Ptr points_in_box_transformed{new pcl::PointCloud<pcl::PointXYZI>};
            pcl::PointCloud<pcl::PointXYZI>::Ptr points_in_box_transformed_2{new pcl::PointCloud<pcl::PointXYZI>};

            pcl::CropBox<pcl::PointXYZI> box_filter;
            Eigen::Vector3f minpoint(min_point_OBB.x, min_point_OBB.y, min_point_OBB.z);
            Eigen::Vector3f maxpoint(max_point_OBB.x, max_point_OBB.y, max_point_OBB.z);

            box_filter.setInputCloud(cloud);

            box_filter.setRotation(euler_angles);
            box_filter.setTranslation(position);

            box_filter.setMin(Eigen::Vector4f(
                config.buffer * min_point_OBB.x, config.buffer * min_point_OBB.y, config.buffer * min_point_OBB.z, 1.0
            ));
            box_filter.setMax(Eigen::Vector4f(
                config.buffer * max_point_OBB.x, config.buffer * max_point_OBB.y, config.buffer * max_point_OBB.x, 1.0
            ));

            box_filter.filter(*points_in_box);
            box_filter.setNegative(true);

            Eigen::Matrix4f transform; // 1st Transformation Matrix
            Eigen::Matrix4f transform_2;
            transform.setIdentity(); // Set to Identity to make bottom row of Matrix
                                     // 0,0,0,1
            transform_2.setIdentity();

            Eigen::Matrix3f R_o;
            Eigen::Matrix3f R_inv;

            R_o << 0, 0, 1, 0, 1, 0, -1, 0, 0;
            // R_o<<1,0,0,0,1,0,0,0,1;

            R_inv = R_o.inverse();
            Eigen::Vector3f translation(1.0, 0, 0);
            Eigen::Vector3f translationback;

            translationback = -R_inv * translation;

            transform.block<3, 3>(0, 0) = rotational_matrix_OBB.inverse();
            transform.block<3, 1>(0, 3) = -rotational_matrix_OBB.inverse() * position;

            transform_2.block<3, 3>(0, 0) = R_o;
            // transform_2.block<3,1>(0,3) <<1.0, 0,0;
            transform_2.block<3, 1>(0, 3) = translation;

            pcl::transformPointCloud(*points_in_box, *points_in_box_transformed, transform);

            //	viewer->addPointCloud(points_in_box_transformed,
            // fildColor_transformed, "OBB3"+j);

            pcl::transformPointCloud(*points_in_box_transformed, *points_in_box_transformed, transform_2);
            // test
            add_oriented_bounding_box(
                viewer,
                position,
                quat,
                OBB_width,
                OBB_height,
                OBB_depth,
                "extractedv4" + std::to_string(jj + 1),
                viewports.v4
            );

            //***********************************
            // Project the ROI to image plane
            //***********************************

            pcl::PointCloud<pcl::PointXYZI>::Ptr unique(new pcl::PointCloud<pcl::PointXYZI>);
            pcl::PointCloud<pcl::PointXYZI> &unique_cloud = *unique;

            pcl::PointCloud<pcl::PointXYZI>::Ptr unique_i(new pcl::PointCloud<pcl::PointXYZI>);
            pcl::PointCloud<pcl::PointXYZI> &unique_cloud_i = *unique_i;

            unique_cloud.width = points_in_box_transformed->points.size();
            unique_cloud.height = 1;
            unique_cloud.is_dense = false;
            unique_cloud.points.resize(unique_cloud.width * unique_cloud.height);

            unique_cloud_i.width = points_in_box_transformed->points.size();
            unique_cloud_i.height = 1;
            unique_cloud_i.is_dense = false;
            unique_cloud_i.points.resize(unique_cloud_i.width * unique_cloud_i.height);

            std::vector<std::vector<float>> unique_points_i;
            std::vector<float> unique_point_i;
            for (const auto &point3 : *points_in_box_transformed) {
                if (point3.x != 0) {
                    unique_point_i = {point3.x, point3.y, point3.z, point3.intensity};
                    unique_points_i.push_back(unique_point_i);
                }
            }
            for (size_t p = 0; p < unique_points_i.size(); ++p) {

                if (unique_points_i[p][3] != 0) {
                    unique_cloud_i.points[p].x = unique_points_i[p][0] * unique_points_i[p][3];
                    unique_cloud_i.points[p].y = unique_points_i[p][1] * unique_points_i[p][3];
                    unique_cloud_i.points[p].z = unique_points_i[p][2] * unique_points_i[p][3];
                    unique_cloud_i.points[p].intensity = unique_points_i[p][3];

                    unique_cloud.points[p].x = unique_points_i[p][0];
                    unique_cloud.points[p].y = unique_points_i[p][1];
                    unique_cloud.points[p].z = unique_points_i[p][2];
                    unique_cloud.points[p].intensity = unique_points_i[p][3];
                }
                // for the livox lidar, no need to conduct this because no intensity
                // value is zero
                else {
                    unique_cloud_i.points[p].x = unique_points_i[p][0];
                    unique_cloud_i.points[p].y = unique_points_i[p][1];
                    unique_cloud_i.points[p].z = unique_points_i[p][2];
                    unique_cloud_i.points[p].intensity = 0.001;

                    unique_cloud.points[p].x = unique_points_i[p][0];
                    unique_cloud.points[p].y = unique_points_i[p][1];
                    unique_cloud.points[p].z = unique_points_i[p][2];
                    unique_cloud.points[p].intensity = unique_points_i[p][3];
                }
            }

            pcl::RangeImage::Ptr range_image_ptr(new pcl::RangeImage);
            pcl::RangeImage &range_image = *range_image_ptr;

            pcl::RangeImage::Ptr range_image_i_ptr(new pcl::RangeImage);
            pcl::RangeImage &range_image_i = *range_image_i_ptr;

            float angularResolution = (float)(config.ang_resolution * (M_PI / 180.0f)); //   1.0 degree in radians
            float maxAngleWidth = (float)(120.0f * (M_PI / 180.0f));                    // 360.0 degree in radians
            float maxAngleHeight = (float)(120.0f * (M_PI / 180.0f));                   // 180.0 degree in radians
            Eigen::Affine3f sensorPose = (Eigen::Affine3f)Eigen::Translation3f(0.0f, 0.0f, 0.0f);
            pcl::RangeImage::CoordinateFrame coordinate_frame = pcl::RangeImage::LASER_FRAME;
            float noiseLevel = 0.00;
            float minRange = 0.0f;
            int borderSize = 1;

            range_image_i.createFromPointCloud(
                unique_cloud_i,
                angularResolution,
                maxAngleWidth,
                maxAngleHeight,
                sensorPose,
                coordinate_frame,
                noiseLevel,
                minRange,
                borderSize
            );

            range_image.createFromPointCloud(
                unique_cloud,
                angularResolution,
                maxAngleWidth,
                maxAngleHeight,
                sensorPose,
                coordinate_frame,
                noiseLevel,
                minRange,
                borderSize
            );

            float *ranges = range_image_i.getRangesArray(); // it is the intensity array actually
            float max = 0;
            float val;

            // normalize the intensity, otherwise we cannot transfer the range image
            // into CV Mat
            for (int i = 0; i < range_image_i.width * range_image_i.height; ++i) {
                val = *(ranges + i);

                if (val < -FLT_MAX || val > FLT_MAX) {

                } else {
                    max = (val > max) ? val : max;
                }
            }

            // Create cv::Mat
            Mat image = Mat(range_image_i.height, range_image_i.width, CV_8UC4);
            unsigned char r, g, b;

// pcl::PointCloud to cv::Mat
#pragma omp parallel for
            for (int y = 0; y < range_image_i.height; y++) {
                for (int x = 0; x < range_image_i.width; x++) {

                    pcl::PointWithRange rangePt = range_image_i.getPoint(x, y);
                    float value = rangePt.range / max;

                    pcl::visualization::FloatImageUtils::getColorForFloat(value, r, g, b);

                    image.at<cv::Vec4b>(y, x)[0] = b;
                    image.at<cv::Vec4b>(y, x)[1] = g;
                    image.at<cv::Vec4b>(y, x)[2] = r;
                    image.at<cv::Vec4b>(y, x)[3] = 255;
                }
            }

            std::stringstream ss1;
            ss1 << "mysesult" << j << ".png";

            imwrite(ss1.str(), image);
            Mat gray;
            Mat gray_flip;
            cvtColor(image, gray, COLOR_BGR2GRAY);

            threshold(gray, gray, config.binary_threshold, 255, THRESH_BINARY);
            // GaussianBlur(gray, gray, Size(3, 3), 25, 0, 4);
            flip(gray, gray_flip, 1);

            // morphologyEx(gray, gray, MORPH_OPEN, kernel);
            std::stringstream ss2;
            ss2 << "grayresult" << j << ".png";
            imwrite(ss2.str(), gray);
            std::stringstream ss3;
            ss3 << "gray_flip" << j << ".png";
            imwrite(ss3.str(), gray_flip);

            //  image_u8_t im = { .width = gray.cols,
            //     .height = gray.rows,
            //     .stride = gray.cols,
            //     .buf = gray.data
            // };

            //  image_u8_t im_flip = { .width = gray_flip.cols,
            //     .height = gray_flip.rows,
            //     .stride = gray_flip.cols,
            //     .buf = gray_flip.data
            // };

            // zarray_t *detections = apriltag_detector_detect(td, &im);
            // zarray_t *detections_flip = apriltag_detector_detect(td, &im_flip);

            pcl::PointWithRange rangePt_ap;
            pcl::PointWithRange point_up;
            pcl::PointWithRange point_down;
            float ratio;

            //***************carry out ArUco
            // detection*****************************************************//
            std::vector<int> markerIds;
            std::vector<std::vector<cv::Point2f>> markerCorners, rejectedCandidates;
            cv::Ptr<cv::aruco::DetectorParameters> parameters = cv::aruco::DetectorParameters::create();
            cv::Ptr<cv::aruco::Dictionary> dictionary =
                cv::aruco::getPredefinedDictionary(cv::aruco::DICT_ARUCO_ORIGINAL);

            cv::aruco::detectMarkers(gray, dictionary, markerCorners, markerIds, parameters, rejectedCandidates);
            // cv::aruco::detectMarkers(im, dictionary, markerCorners, markerIds,
            // parameters, rejectedCandidates);
            // cv::Mat outputImage = gray.clone();
            // cv::aruco::drawDetectedMarkers(outputImage, markerCorners, markerIds);

            // cout<<"ArUco"<<markerIds.at(0)<<"\n";

            if (markerIds.size() > 0) {
                if (markerIds.at(0) > 500) {

                    cv::aruco::detectMarkers(
                        gray_flip, dictionary, markerCorners, markerIds, parameters, rejectedCandidates
                    );
                    use_flip = true;
                    // cout<<"ArUco"<<markerIds.at(0)<<"\n";
                }
            }

            if (!(markerIds.empty()) && !use_flip) {

                id_num.push_back(markerIds.at(0));
                for (int l = 0; l < markerCorners.size(); l++) {
                    for (int ll = 0; ll < 4; ll++) {

                        for (int mm = 0; mm < 4; mm++) {
                            if (range_image.isObserved(
                                    round(markerCorners.at(l)[ll].x), round(markerCorners.at(l)[ll].y + mm)
                                )) {
                                if (range_image.isObserved(
                                        round(markerCorners.at(l)[ll].x), round(markerCorners.at(l)[ll].y - mm)
                                    ))

                                {

                                    range_image.calculate3DPoint(
                                        round(markerCorners.at(l)[ll].x),
                                        round(markerCorners.at(l)[ll].y + mm),
                                        point_up
                                    );
                                    range_image.calculate3DPoint(
                                        round(markerCorners.at(l)[ll].x),
                                        round(markerCorners.at(l)[ll].y - mm),
                                        point_down
                                    );
                                    ratio = point_down.range / point_up.range;
                                    Eigen::Vector3f temp_vertex(
                                        point_up.x * (1 / (1 + ratio)) + point_down.x * (ratio / (1 + ratio)),
                                        point_up.y * (1 / (1 + ratio)) + point_down.y * (ratio / (1 + ratio)),
                                        point_up.z * (1 / (1 + ratio)) + point_down.z * (ratio / (1 + ratio))
                                    );

                                    Eigen::Vector3f final_vertex;
                                    final_vertex = R_inv * temp_vertex + translationback;

                                    final_vertex = rotational_matrix_OBB * final_vertex + position;

                                    std::vector<float> final_vertex_std;
                                    final_vertex_std = {final_vertex[0], final_vertex[1], final_vertex[2]};
                                    pts1.push_back(final_vertex_std);

                                    break;

                                } // uppers point

                            } // down point

                        } // nearest 4 points
                    }
                }
            }

            //-----------------------------------------------------------------------------------

            if (!(markerIds.empty()) && use_flip) {
                use_flip = false;
                id_num.push_back(markerIds.at(0));
                for (int l = 0; l < markerCorners.size(); l++) {

                    for (int ll = 0; ll < 4; ll++) {

                        // if the vertex is unobserved.
                        for (int mm = 0; mm < 4; mm++) {
                            if (range_image.isObserved(
                                    round(range_image_i.width - markerCorners.at(l)[ll].x),
                                    round(markerCorners.at(l)[ll].y + mm)
                                )) {
                                if (range_image.isObserved(
                                        round(range_image_i.width - markerCorners.at(l)[ll].x),
                                        round(markerCorners.at(l)[ll].y - mm)
                                    ))

                                {

                                    range_image.calculate3DPoint(
                                        round(range_image_i.width - markerCorners.at(l)[ll].x),
                                        round(markerCorners.at(l)[ll].y + mm),
                                        point_up
                                    );
                                    range_image.calculate3DPoint(
                                        round(range_image_i.width - markerCorners.at(l)[ll].x),
                                        round(markerCorners.at(l)[ll].y - mm),
                                        point_down
                                    );
                                    ratio = point_down.range / point_up.range;
                                    Eigen::Vector3f temp_vertex(
                                        point_up.x * (1 / (1 + ratio)) + point_down.x * (ratio / (1 + ratio)),
                                        point_up.y * (1 / (1 + ratio)) + point_down.y * (ratio / (1 + ratio)),
                                        point_up.z * (1 / (1 + ratio)) + point_down.z * (ratio / (1 + ratio))
                                    );

                                    Eigen::Vector3f final_vertex;
                                    final_vertex = R_inv * temp_vertex + translationback;

                                    final_vertex = rotational_matrix_OBB * final_vertex + position;

                                    std::vector<float> final_vertex_std;
                                    final_vertex_std = {final_vertex[0], final_vertex[1], final_vertex[2]};
                                    pts1.push_back(final_vertex_std);

                                    break;

                                } // uppers point

                            } // down point

                        } // nearest 4 points

                        //   if (range_image.isObserved
                        //   (round(range_image_i.width-markerCorners.at(l)[ll].x),round(markerCorners.at(l)[ll].y)))

                        // 	 {//if the vertex is observed

                        //     	range_image.calculate3DPoint(round(range_image_i.width-markerCorners.at(l)[ll].x),round(markerCorners.at(l)[ll].y),
                        //     rangePt_ap);

                        //     	Eigen::Vector3f
                        //     temp_vertex(rangePt_ap.x,rangePt_ap.y,rangePt_ap.z);
                        //     	Eigen::Vector3f final_vertex;
                        //     	final_vertex = R_inv*temp_vertex+ translationback;

                        //     	final_vertex = rotational_matrix_OBB*final_vertex+
                        //     position;

                        //     	std::vector<float> final_vertex_std;
                        //     	final_vertex_std={final_vertex[0],final_vertex[1],final_vertex[2]};
                        //     	pts1.push_back(final_vertex_std);

                        //      }
                        // cout<<"one done"<<"\n";
                    }
                }
            }

            add_points_in_box(viewer, points_in_box, "points_in_box" + std::to_string(jj), viewports.v5);
            // We could also add points_in_box_transformed if needed

            j++; // only the vaild candidates are preseved in this {}

        } // if it is a vaild candidate

    } // the loop the go through the feature clusters

    add_marker_vertices(viewer, pts1, viewports.v6);

    std::cout << "Number of detected marker(s) is"
              << "\t" << id_num.size() << "."
              << "\n";

    if (id_num.size() == 0) {
        std::cout << "No aruco is detected."
                  << "\n";
    }

    if (id_num.size() != 0) {
        std::cout << "----------------------Marker Detection-----------------------"
                  << "\n";
        for (int i = 0; i < id_num.size(); i++) {
            std::cout << "ID:" << id_num[i] << "\n";
            std::cout << "Location of the vertices with respect to the world "
                         "coordinate system (m):"
                      << endl;
            std::cout << "first vertex"
                      << "\t"
                      << "x=" << pts1[4 * i][0] << "\t"
                      << "y=" << pts1[4 * i][1] << "\t"
                      << "z=" << pts1[4 * i][2] << "\n";
            std::cout << "second vertex"
                      << "\t"
                      << "x=" << pts1[4 * i + 1][0] << "\t"
                      << "y=" << pts1[4 * i + 1][1] << "\t"
                      << "z=" << pts1[4 * i + 1][2] << "\n";
            std::cout << "thrid vertex"
                      << "\t"
                      << "x=" << pts1[4 * i + 2][0] << "\t"
                      << "y=" << pts1[4 * i + 2][1] << "\t"
                      << "z=" << pts1[4 * i + 2][2] << "\n";
            std::cout << "fourth vertex"
                      << "\t"
                      << "x=" << pts1[4 * i + 3][0] << "\t"
                      << "y=" << pts1[4 * i + 3][1] << "\t"
                      << "z=" << pts1[4 * i + 3][2] << "\n";
        }
    }

    while (!viewer->wasStopped()) {
        viewer->spinOnce();
    }

    // viewer->spin();

    return 0;
}
