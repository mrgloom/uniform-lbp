/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2014, Itseez Inc, all rights reserved.
// Third Recty copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a Recticular purpose are disclaimed.
// In no event shall the Itseez Inc or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "opencv2/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/datasets/fr_lfw.hpp"

#include "texturefeature.h"
#include "preprocessor.h"

#if 0
 #include "../profile.h"
#else
 #define PROFILE ;
 #define PROFILEX(s) ;
#endif

#include <iostream>
#include <cstdio>
#include <string>
#include <fstream>
#include <vector>
#include <map>
#include <set>

using namespace std;
using namespace cv;
using namespace cv::datasets;

map<string, int> people;

int getLabel(const string &imagePath)
{
    size_t pos = imagePath.find('/');
    string curr = imagePath.substr(0, pos);
    map<string, int>::iterator it = people.find(curr);
    if (people.end() == it)
    {
        people.insert(make_pair(curr, (int)people.size()));
        it = people.find(curr);
    }
    return (*it).second;
}

void printOptions()
{
    cerr << "[extractors]  :"<< endl;
    for (size_t i=0; i<TextureFeature::EXT_MAX; ++i) {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::EXS[i],i); }
    cerr << endl << endl << "[filters] :" << endl;
    for (size_t i=0; i<TextureFeature::FIL_MAX; ++i) {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::FILS[i],i); }
    cerr << endl << endl << "[classifiers] :" << endl;
    for (size_t i=0; i<TextureFeature::CL_MAX; ++i)  {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::CLS[i],i);  }
    //cerr << endl << endl <<  "[preproc] :" << endl;
    //for (size_t i=0; i<TextureFeature::PRE_MAX; ++i) {  if(i%5==0) cerr << endl; cerr << format("%10s(%2d)",TextureFeature::PPS[i],i);  }
    cerr << endl;
}



class MyFace
{
    Ptr<TextureFeature::Extractor> ext;
    Ptr<TextureFeature::Filter>  fil;
    Ptr<TextureFeature::Verifier>  ver;
    Ptr<TextureFeature::Classifier>  cls;
    Preprocessor pre;

    Mat labels;
    Mat features;
    int nimg;

public:

    MyFace(int extract=0, int filt=0, int clsfy=0, int preproc=0, int crop=0, const String &train="dev",int skip=1, bool lab=false)
        : pre(preproc,crop)
        , nimg(train=="dev"?((4400/skip)^0x1):(10800/skip)^0x01)
    {
        ext = TextureFeature::createExtractor(extract);
        fil = TextureFeature::createFilter(filt);
        if (lab)
            cls = TextureFeature::createClassifier(clsfy);
        else
            ver = TextureFeature::createVerifier(clsfy);
    }

    Mat extract(const Mat & a) const
    {
        Mat feat1;
        ext->extract(pre.process(a), feat1);

        if (feat1.type() != CV_32F)
            feat1.convertTo(feat1,CV_32F);
        return feat1.reshape(1,1);
    }

    virtual int addTraining(const Mat & img, int label)
    {
        Mat feat = extract(img);
        if (! fil.empty())
        {
            fil->filter(feat,feat);
        }
        if ( features.empty() )
        {
            features = Mat(nimg, feat.total(), feat.type());
        }
        feat.copyTo(features.row(labels.rows));
        labels.push_back(label);
        cerr << feat.cols << " i_" << labels.rows << "\r";
        return labels.rows;
    }
    virtual bool train()
    {
        //cerr << "\n." << features.cols << " ";
        //cerr << "start training." << " ";
        int ok = 0;
        if (!cls.empty())
            ok = cls->train(features, labels.reshape(1,features.rows));
        if (!ver.empty())
            ok = ver->train(features, labels/*.reshape(1,features.rows)*/);
        //cerr << "done training." << endl;
        CV_Assert(ok);
        features.release();
        labels.release();
        return ok!=0;
    }
    virtual int same(const Mat & a, const Mat &b) const
    {
        Mat feat1 = extract(a);
        Mat feat2 = extract(b);

        if (! fil.empty())
        {
            fil->filter(feat1,feat1);
            fil->filter(feat2,feat2);
        }

        if (!ver.empty())
            return ver->same(feat1,feat2);

        Mat_<float> r1,r2;
        cls->predict(feat1,r1);
        cls->predict(feat2,r2);
        //cerr << format("%4d %4d\t",int(r1(0)),int(r2(0)));
        return int(r1(0)) == int(r2(0));
    }
};


int main(int argc, const char *argv[])
{
    PROFILE;
    const char *keys =
            "{ help h usage ? |    | show this message }"
            "{ opts o         |    | show extractor / filter / verifier options }"
            "{ path p         |data/lfw-deepfunneled/| path to dataset (lfw2 folder) }"
            "{ ext e          |27   | extractor enum }"
            "{ fil f          |0   | filter enum }"
            "{ cls c          |21   | classifier enum }"
            "{ pre P          |0   | preprocessing }"
            "{ lab l          |0   | train / test with labels(instead of direct image compare) }"
            "{ skip s         |80  | skip imgs for train }"
            "{ crop C         |80  | cut outer 80 pixels to to 90x90 }"
            "{ train t        |dev | train method: 'dev'(pairsDevTrain.txt) or 'split'(pairs.txt) }";

    CommandLineParser parser(argc, argv, keys);
    string path(parser.get<string>("path"));
    if (parser.has("help") || path=="true")
    {
        parser.printMessage();
        return -1;
    }
    if (parser.has("opts"))
    {
        printOptions();
        return -1;
    }
    bool lab = parser.has("lab");
    int ext = parser.get<int>("ext");
    int fil = parser.get<int>("fil");
    int cls = parser.get<int>("cls");
    int pre = parser.get<int>("pre");
    int crp = parser.get<int>("crop");
    int skip = parser.get<int>("skip");
    string trainMethod(parser.get<string>("train"));
    cout << TextureFeature::EXS[ext] << " " << TextureFeature::FILS[fil] << " " << TextureFeature::CLS[cls] << " " << crp << " " << trainMethod << (lab?" c":" v") << endl;

    int64 t0 = getTickCount();
    Ptr<MyFace> model = makePtr<MyFace>(ext,fil,cls,pre,crp,trainMethod,skip,lab);

    // load dataset
    Ptr<FR_lfw> dataset = FR_lfw::create();
    dataset->load(path);
    unsigned int numSplits = dataset->getNumSplits();

    if (trainMethod == "dev") // train on personsDevTrain.txt
    {
        int n = int(dataset->getTrain().size())-skip;
        for (int i=0; i<n; i+=skip)
        {
            FR_lfwObj *example = static_cast<FR_lfwObj *>(dataset->getTrain()[i].get());

            int currNum1 = getLabel(example->image1);
            Mat img = imread(path+example->image1, IMREAD_GRAYSCALE);
            model->addTraining(img, currNum1);

            int currNum2 = getLabel(example->image2);
            img = imread(path+example->image2, IMREAD_GRAYSCALE);
            model->addTraining(img, currNum2);
        }

        {
            PROFILEX("train");
            model->train();
        }
    }


    vector<double> p_acc, p_tpr, p_fpr;
    for (unsigned int j=0; j<numSplits; ++j)
    {
        PROFILEX("splits");
        if (trainMethod == "split") // train on the remaining 9 splits from pairs.txt
        {
            for (unsigned int j2=0; j2<numSplits; j2++)
            {
                if (j==j2) continue;

                vector < Ptr<Object> > &curr = dataset->getTest(j2);
                for (unsigned int i=0; i<curr.size(); i+=skip)
                {
                    FR_lfwObj *example = static_cast<FR_lfwObj *>(curr[i].get());
                    int currNum1 = getLabel(example->image1);
                    Mat img = imread(path+example->image1, IMREAD_GRAYSCALE);
                    model->addTraining(img, currNum1);

                    int currNum2 = getLabel(example->image2);
                    img = imread(path+example->image2, IMREAD_GRAYSCALE);
                    model->addTraining(img, currNum2);
                }
            }
            {
                PROFILEX("train");
                model->train();
            }
        }

        unsigned int incorrect[2] = {0}, correct[2] = {0};
        vector < Ptr<Object> > &curr = dataset->getTest(j);
        for (unsigned int i=0; i<curr.size(); i+=skip)
        {
            PROFILEX("tests");
            FR_lfwObj *example = static_cast<FR_lfwObj *>(curr[i].get());
            //cerr << i << "\t";
            Mat img1 = imread(path+example->image1, IMREAD_GRAYSCALE);
            Mat img2 = imread(path+example->image2, IMREAD_GRAYSCALE);
            bool same = model->same(img1,img2)>0;
            if (same == example->same)
                correct[example->same]++;
            else
                incorrect[example->same]++;
            //cerr << same << " " << example->same << "                 \r";
        }

        double acc = double(correct[1]+correct[0])/((curr.size()/skip));
        double tpr = double(correct[1])/(correct[1]+incorrect[1]);
        double fpr = double(incorrect[0])/(correct[0]+incorrect[0]);
        printf("%4u %2.3f/%-2.3f  %2.3f                        \n", j, tpr,fpr,acc );
        p_acc.push_back(acc);
        p_tpr.push_back(tpr);
        p_fpr.push_back(fpr);
    }

    double mu_acc = 0.0, mu_tpr=0.0, mu_fpr=0.0;
    for (size_t i=0; i<p_acc.size(); ++i)
    {
        mu_acc += p_acc[i];
        mu_tpr += p_tpr[i];
        mu_fpr += p_fpr[i];
    }
    mu_acc /= p_acc.size();
    mu_tpr /= p_tpr.size();
    mu_fpr /= p_fpr.size();
    double sigma = 0.0;
    for (vector<double>::iterator it=p_acc.begin(); it!=p_acc.end(); ++it)
    {
        sigma += (*it - mu_acc)*(*it - mu_acc);
    }
    sigma = sqrt(sigma/p_acc.size());
    double se = sigma/sqrt(double(p_acc.size()));

    int64 t1 = getTickCount();
    cerr << format("%-8s",TextureFeature::EXS[ext])  << " ";
    cerr << format("%-7s",TextureFeature::FILS[fil]) << " ";
    cerr << format("%-7s",TextureFeature::CLS[cls])  << " ";
    //cerr << format("%-8s",TextureFeature::PPS[pre])  << " ";
    cerr << format("%-5s",trainMethod.c_str()) << "\t";
    //cerr << format("%2d %d %-6s",crp ,flp, trainMethod.c_str()) << "\t";
    cerr << format("%3.4f/%-3.4f %3.4f/%-3.4f %3.4f",  mu_acc, se, mu_tpr, mu_fpr, ((t1-t0)/getTickFrequency())) << endl;

    return 0;
}
