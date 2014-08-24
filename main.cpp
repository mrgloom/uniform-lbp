#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core/utility.hpp>
using namespace cv;


#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
using namespace std;


#include "TextureFeature.h"
#include "extractDB.h"

enum 
{
    TEST_ROC,   // 1:n random split, calc tp,fp,tn,fn for ROC analysis, write out matlab files.
    TEST_CROSS, // n-fold cross validation
};

int testMethod = TEST_CROSS;


//
// the current compilation of extractors / classifiers. (should probably go into a header)
//

extern cv::Ptr<TextureFeature::Extractor> createExtractorPixels(int resw=0, int resh=0);
extern cv::Ptr<TextureFeature::Extractor> createExtractorMoments();
extern cv::Ptr<TextureFeature::Extractor> createExtractorLbp(int gridx=8, int gridy=8, int u_table=-1);
extern cv::Ptr<TextureFeature::Extractor> createExtractorBGC1(int gx=8, int gy=8, int utable=-1);
extern cv::Ptr<TextureFeature::Extractor> createExtractorWLD(int gx=8, int gy=8, int tf=CV_32F);
extern cv::Ptr<TextureFeature::Extractor> createExtractorLQP(int gx=8, int gy=8);
extern cv::Ptr<TextureFeature::Extractor> createExtractorRT(int gx=8, int gy=8, int utable=-1);

extern cv::Ptr<TextureFeature::Classifier> createClassifierNearest(int norm_flag=NORM_L2);
extern cv::Ptr<TextureFeature::Classifier> createClassifierHist(int flag=HISTCMP_CHISQR);
extern cv::Ptr<TextureFeature::Classifier> createClassifierKNN(int n=1);                // TODO: needs a way to get to the k-1 others
extern cv::Ptr<TextureFeature::Classifier> createClassifierSVM(double degree = 0.5,double gamma = 0.8,double coef0 = 0,double C = 0.99, double nu = 0.2, double p = 0.5);
//extern cv::Ptr<TextureFeature::Classifier> createClassifierBayes();                     // TODO: slow
//extern cv::Ptr<TextureFeature::Classifier> createClassifierRTrees();                    // TODO: broken
//extern cv::Ptr<TextureFeature::Classifier> createClassifierDTree();                     // TODO: broken



double ct(int64 t) {    return double(t) / cv::getTickFrequency(); }





struct ROC
{
    int tp,fp,tn,fn;
    ROC() : tp(0),fp(0),tn(0),fn(0) {}

    int p() { return tp+fn; }
    int n() { return fp+tn; }
    float tpr() { return p()>0 ? (float(tp) / p()) : 0.0f; }
    float fpr() { return n()>0 ? (float(fp) / n()) : 0.0f; }
    float acc() { return float(tp+tn)/(p()+n()); }

    string str() { return format("%3d %3d %3d %3d : %3.3f : %3.3f %3.3f",tp,fp,tn,fn,acc(),tpr(),fpr()); }
};


using TextureFeature::Extractor;
using TextureFeature::Classifier;


RNG rng(getTickCount());

void random_roc(vector<Point2f> & points, Ptr<Extractor> ext, Ptr<Classifier> cls, const vector<Mat>& images, const vector<int>& labels, const vector<vector<int>>& persons, int ratio, int iterations, bool verbose=false)
{
    for ( int c=0; c<iterations; c++ )
    {
        Mat train_data;
        Mat train_labels;
        Mat test_data;
        Mat test_labels;

        // split test/train data 1/ratio
        for ( size_t i=0; i<images.size(); i++ )
        {
            Mat feature;
            ext->extract(images[i],feature);
            if ( rng.uniform(0,ratio) == 0 )
            {
                test_data.push_back(feature);
                test_labels.push_back(labels[i]);
            }
            else 
            {
                train_data.push_back(feature);
                train_labels.push_back(labels[i]);
            }
        }
        test_data = test_data.reshape(1,test_labels.rows);
        train_data = train_data.reshape(1,train_labels.rows);

        cls->train(train_data,train_labels);
        if (verbose) cerr << ".(" << images.size() <<","<< train_data.rows<<","<<test_data.rows<<") ";


        ROC roc;
        int npos = 0;
        int test_id = test_labels.at<int>( rng.uniform(0,test_labels.rows) );
        for ( int i=0; i<test_data.rows; i++ )
        {
            Mat res;
            cls->predict(test_data.row(i), res);

            int pred = int(res.at<float>(0));
            int ground  = test_labels.at<int>(i);

            roc.tp += ((test_id==pred) && (test_id==ground));
            roc.fp += ((test_id==pred) && (test_id!=ground));
            roc.tn += ((test_id!=pred) && (test_id!=ground));
            roc.fn += ((test_id!=pred) && (test_id==ground));
            npos += (test_id == ground);
        }
        Point2f p(roc.tpr(),roc.fpr());
        points.push_back(p);
        if (verbose) cerr << test_id << "("<<npos<<")\t: " << roc.str() << endl;
    }
}




void runtest(string name, Ptr<Extractor> ext, Ptr<Classifier> cls, const vector<Mat>& images, const vector<int>& labels, const vector<vector<int>>& persons, size_t fold=10, bool verbose=false ) 
{
    if (testMethod == TEST_ROC)
    {
        vector<Point2f> points;
        random_roc(points,ext,cls,images,labels,persons,fold,100,verbose);
        cerr << name << endl;
        
        // write 1d tpr,fpr arrays to (matlab) file:
        string fn = name + ".roc";
        ofstream of(fn.c_str());
        of << " # name: tpr_" << name << endl;
        of << " # type: matrix" << endl;
        of << " # rows: 1" << endl;
        of << " # columns: " << points.size() << endl;   
        for ( size_t i=0; i<points.size(); i++)
            of << points[i].x << " ";
        of << endl << endl;
        of << " # name: fpr_" << name << endl;
        of << " # type: matrix" << endl;
        of << " # rows: 1" << endl;
        of << " # columns: " << points.size() << endl;   
        for ( size_t i=0; i<points.size(); i++)
            of << points[i].y << " ";
        of << endl << endl;
        of.close(); 
        return;
    }


    //
    // for each fold, take alternating n/fold items for test, the others for training
    //
    // each test is confused on its own over a lot of folds..
    Mat confusion = Mat::zeros(persons.size(),persons.size(),CV_32F);
    vector<float> tpr;
    vector<float> fnr;

    for ( size_t f=0; f<fold; f++ )
    {
        Mat trainFeatures;
        Mat trainLabels;
        vector<Mat> testFeatures;
        Mat testLabels;

        int64 t1 = cv::getTickCount();
        // split train/test set per person:
        for ( size_t j=0; j<persons.size(); j++ )
        {
            size_t n_per_person = persons[j].size();
            if (n_per_person < fold)
                continue;
            int r = (fold != 0) ? (n_per_person/fold) : -1;
            for ( size_t n=0; n<n_per_person; n++ )
            {
                int index = persons[j][n];

                Mat feature;
                ext->extract(images[index],feature);

                if ( (fold>1) && (n >= f*r) && (n <= (f+1)*r) ) // sliding window per fold
                {
                    testFeatures.push_back(feature);
                    testLabels.push_back(labels[index]);
                }
                else
                {
                    trainFeatures.push_back(feature);
                    trainLabels.push_back(labels[index]);
                }
            }
        }
        cls->train(trainFeatures.reshape(1,trainLabels.rows),trainLabels);

        Mat conf = Mat::zeros(confusion.size(), CV_32F);
        for ( size_t i=0; i<testFeatures.size(); i++ )
        {
            Mat res;
            cls->predict(testFeatures[i].reshape(1,1), res);
    
            int pred = int(res.at<float>(0));
            int ground = testLabels.at<int>(i);
            conf.at<float>(ground, pred) ++;
        }
        confusion += conf;

        cout << '.';
        double all = sum(conf)[0], neg = all - sum(conf.diag())[0];
        if ( verbose ) cerr << format(" %-16s %3d %5d %d",name.c_str(), f, int(all-neg), int(neg)) << endl;
    }


    // evaluate. this is probably all too simple.
    double all = sum(confusion)[0];
    double neg = all - sum(confusion.diag())[0];
    double err = double(neg)/all;
    //if ( verbose ) cerr << confusion << endl;
    cout << format(" %-16s %6d %6d %6.3f",name.c_str(), int(all-neg), int(neg), (1.0-err)) << endl;

}

//
//
// face att.txt 5     5      1         1
// face db      fold  reco  verbose  preprocessing
//
// special: reco==0 will run *all* recognizers available on a given db
//
int main(int argc, const char *argv[]) 
{
    vector<Mat> images;
    Mat labels;

    std::string db_path("yale.txt");
    //std::string db_path("att.txt");
    //std::string db_path("yale.txt");
    if ( argc>1 ) db_path = argv[1];

    size_t fold = 4;
    if ( argc>2 ) fold = atoi(argv[2]);

    int rec = 2;
    if ( argc>3 ) rec = atoi(argv[3]);

    bool verbose = false;
    if ( argc>4 ) verbose = (argv[4][0] != '0');

    int preproc = 0; // 0-none 1-eqhist 2-tan_triggs 3-clahe
    if ( argc>5 ) preproc = atoi(argv[5]);

    if ( argc>6 ) testMethod = atoi(argv[6]); // TEST_TOC <> TEST_CROSS

    extractDB(db_path,images,labels,preproc,400);

    // per person id lookup
    vector<vector<int>> persons;
    setupPersons( labels, persons );

    fold = std::min(fold,images.size()/persons.size());

    cout << fold  << " fold, " ;
    cout << persons.size()  << " classes, " ;
    cout << images.size() << " images, ";
//    cout << images.size()/persons.size() << " per class, ";
    char *pp[] = { "no preproc", "equalizeHist", "tan-triggs", "CLAHE" };
    cout << pp[preproc];
    cout << endl;

    int n=23;
    if ( rec > 0 ) // loop through all possibilities for 0, restrict it to the chosen one else.
    {
        n = rec+1;
    }
    for ( ; rec<n; rec++ ) 
    {
        switch(rec)
        {
        default: continue;
        case 1: runtest("pixels", createExtractorPixels(120,120), createClassifierNearest(), images,labels,persons, fold,verbose); break;
        case 2: runtest("lbp",    createExtractorLbp(),           createClassifierNearest(), images,labels,persons, fold,verbose); break;
        case 3: runtest("lbpu",   createExtractorLbp(8,8,0),      createClassifierNearest(), images,labels,persons, fold,verbose); break;
        //case 4:  runtest("lbpu_mod",createExtractorLbpUniform(8,8,1), createClassifierNearest(), images,labels,persons, fold,verbose); break;
        //case 5:  runtest("lbpu_red",createExtractorLbpUniform(8,8,2), createClassifierNearest(), images,labels,persons, fold,verbose); break;
        //case 6:  runtest("lbpu_svm",createExtractorLbpUniform(8,8,0), createClassifierSVM(), images,labels,persons, fold,verbose); break;
        case 7: runtest("lbpu_red_svm", createExtractorLbp(8,8,2), createClassifierSVM(),    images,labels,persons, fold,verbose); break;
        case 8: runtest("lbp_chisqr",   createExtractorLbp(),      createClassifierHist(),   images,labels,persons, fold,verbose); break;
        case 9: runtest("lbp_hell",     createExtractorLbp(),      createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose);   break;
        case 10: runtest("lbpu_hell",   createExtractorLbp(),      createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose);   break;
        case 11: runtest("lbpu_red_hell", createExtractorLbp(8,8,2), createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose); break;
        case 12: runtest("bgc1_hell",   createExtractorBGC1(),     createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose);   break;
        case 13: runtest("bgc1_red_hell", createExtractorBGC1(8,8,2), createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose);break;
        //case 15: runtest("wld_L1",      createExtractorWLD(8,8,CV_8U),createClassifierNearest(NORM_L1),  images,labels,persons, fold,verbose);      break;
        case 16: runtest("wld_hell",    createExtractorWLD(),      createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose); break;
        // case 17: runtest("rt_hell", createExtractorRT(), createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose); break;
        //case 17: runtest("lqp_hell", createExtractorLQP(), createClassifierHist(HISTCMP_HELLINGER), images,labels,persons, fold,verbose); break;
        }
    }
    
    return 0;
}


