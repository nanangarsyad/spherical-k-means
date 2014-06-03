/* File: spkmeans.cpp
 *
 * A parallel implementation of the Spherical K-Means algorithm using the
 * Galois library (URL_HERE).
 */


#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

#include "Galois/Galois.h"
#include "Galois/Timer.h"

#include "vectors.h"


#define DEFAULT_K 2
#define DEFAULT_THREADS 2
#define Q_THRESHOLD 0.001


using namespace std;



// Debug: prints the given vector (array) to std out.
void printVec(float *vec, int size)
{
    for(int i=0; i<size; i++)
        cout << vec[i] << " ";
    cout << endl;
}



// Applies the TXN scheme to each document vector of the given matrix.
// TXN effectively just normalizes each of the document vectors.
void txnScheme(float **doc_matrix, int dc, int wc)
{
    for(int i=0; i<dc; i++)
        vec_normalize(doc_matrix[i], wc);
}



// Cleans partition memory, resetting the partitions list.
void clearPartitions(float ***partitions, int k)
{
    for(int i=0; i<k; i++)
        delete partitions[i];
}



// Cleans concept vector memory, resetting the concepts list.
void clearConcepts(float **concepts, int k)
{
    for(int i=0; i<k; i++)
        delete concepts[i];
}



// Returns the quality of the given partition by doing a dot product against
// its given concept vector.
float computeQuality(float **partition, int p_size, float *concept, int wc)
{
    float *sum_p = vec_sum(partition, wc, p_size);
    float quality = vec_dot(sum_p, concept, wc);
    delete sum_p;
    return quality;
}



// Returns the total quality of all partitions by summing the qualities of
// each individual partition.
float computeQuality(float ***partitions, int *p_sizes, float **concepts,
    int k, int wc)
{
    float quality = 0;
    for(int i=0; i<k; i++)
        quality += computeQuality(partitions[i], p_sizes[i], concepts[i], wc);
    return quality;
}



// Computes the cosine similarity value of the two given vectors (dv and cv).
float cosineSimilarity(float *dv, float *cv, int wc)
{
    return vec_dot(dv, cv, wc) / (vec_norm(dv, wc) * vec_norm(cv, wc));
}



// Computes the concept vector of the given partition. A partition is an array
// of document vectors, and the concept vector will be allocated and populated.
float* computeConcept(float **partition, int p_size, int wc)
{
    float *cv = vec_sum(partition, wc, p_size);
    vec_multiply(cv, wc, (1.0 / wc));
    vec_divide(cv, wc, vec_norm(cv, wc));
    return cv;
}



// Results struct can contain partition and concept vector pointers, and
// a function to clean out the memory.
struct Results
{
    // clustering variables (k, word count, document count)
    int k;
    int dc;
    int wc;

    // pointers to partitions and concepts
    float ***partitions;
    int *p_sizes;
    float **concepts;

    // Constructor: pass in the three required values (k, wc, dc), and set
    // partition and concept vector pointers optionally.
    Results(int k_, int dc_, int wc_,
            float ***ps_ = 0, int *psz_ = 0, float **cvs_ = 0) {
        k = k_;
        dc = dc_;
        wc = wc_;
        partitions = ps_;
        p_sizes = psz_;
        concepts = cvs_;
    }

    // Destructor: calls its own clean up function
    ~Results() {
        clearMemory();
    }

    // clean up the partitions and concept vector pointers
    void clearMemory() {
        if(partitions != 0) {
            clearPartitions(partitions, k);
            delete partitions;
            partitions = 0;
        }
        if(p_sizes != 0) {
            delete p_sizes;
            p_sizes = 0;
        }
        if(concepts != 0) {
            clearConcepts(concepts, k);
            delete concepts;
            concepts = 0;
        }
    }
};



// Runs the spherical k-means algorithm on the given sparse matrix D and
// clusters the data into k partitions.
Results runSPKMeans(float **doc_matrix, unsigned int k, int dc, int wc)
{
    // keep track of the run time for this algorithm
    Galois::Timer timer;
    timer.start();


    // apply the TXN scheme on the document vectors (normalize them)
    txnScheme(doc_matrix, dc, wc);


    // initialize arrays
    float ***partitions = new float**[k];
    int *p_sizes = new int[k];
    float **concepts = new float*[k];


    // create the first arbitrary partitioning
    int split = floor(dc / k);
    cout << "Split = " << split << endl;
    int base = 1;
    for(int i=0; i<k; i++) {
        int top = base + split - 1;
        if(i == k-1)
            top = dc;

        int p_size = top - base + 1;
        p_sizes[i] = p_size;
        cout << "Created new partition of size " << p_size << endl;

        partitions[i] = new float*[p_size];
        for(int j=0; j<p_size; j++)
            partitions[i][j] = doc_matrix[base + j - 1];

        base = base + split;
    }


    // compute concept vectors
    for(int i=0; i<k; i++)
        concepts[i] = computeConcept(partitions[i], p_sizes[i], wc);


    // compute initial quality of the partitions
    float quality = computeQuality(partitions, p_sizes, concepts, k, wc);
    cout << "Initial quality: " << quality << endl;


    // do spherical k-means loop
    float dQ = Q_THRESHOLD * 10;
    int iterations = 0;
    while(dQ > Q_THRESHOLD) {
        iterations++;

        // compute new partitions based on old concept vectors
        vector<float*> *new_partitions = new vector<float*>[k];
        for(int i=0; i<k; i++)
            new_partitions[i] = vector<float*>();
        for(int i=0; i<dc; i++) {
            int cIndx = 0;
            float cVal = cosineSimilarity(doc_matrix[i], concepts[0], wc);
            for(int j=1; j<k; j++) {
                float new_cVal = cosineSimilarity(doc_matrix[i], concepts[j], wc);
                if(new_cVal > cVal) {
                    cVal = new_cVal;
                    cIndx = j;
                }
            }
            new_partitions[cIndx].push_back(doc_matrix[i]);
        }

        // transfer the new partitions to the partitions array
        clearPartitions(partitions, k);
        for(int i=0; i<k; i++) {
            partitions[i] = new_partitions[i].data();
            p_sizes[i] = new_partitions[i].size();
        }

        // compute new concept vectors
        clearConcepts(concepts, k);
        for(int i=0; i<k; i++)
            concepts[i] = computeConcept(partitions[i], p_sizes[i], wc);

        // compute quality of new partitioning
        float n_quality = computeQuality(partitions, p_sizes, concepts, k, wc);
        dQ = n_quality - quality;
        quality = n_quality;
        cout << "Quality: " << quality << " (+" << dQ << ")" << endl;
    }


    // report runtime statistics
    timer.stop();
    float time_in_ms = timer.get();
    cout << "Done in " << time_in_ms / 1000
         << " seconds after " << iterations << " iterations." << endl;


    // return the resulting partitions and concepts in a Results struct
    Results r(k, dc, wc, partitions, p_sizes, concepts);
    return r;
}



// shows the results
void displayResults(Results *r, char **words, int num_to_show = 10)
{
    // make sure num_to_show doesn't exceed the actual word count
    if(num_to_show > r->wc)
        num_to_show = r->wc;

    // for each partition, sum the weights of each word, and show the top
    //  words that occur in the partition:
    for(int i=0; i<(r->k); i++) {
        cout << "Partition #" << (i+1) << ":" << endl;
        // sum the weights
        float *sum = vec_sum(r->partitions[i], r->wc, r->p_sizes[i]);

        // sort this sum using C++ priority queue (keeping track of indices)
        vector<float> values(sum, sum + r->wc);
        priority_queue<pair<float, int>> q;
        for(int i=0; i<values.size(); i++)
            q.push(pair<float, int>(values[i], i));

        // show top num_to_show words
        for(int i=0; i<num_to_show; i++) {
            int index = q.top().second;
            cout << "   " << words[index] << endl;
            q.pop();
        }
    }
}



/* Read the document data file into a spare matrix (2D array) format).
 * This function assumes that the given file name is valid.
 * Returns the 2D array (sparse matrix) - columns are document vectors.
 * FILE FORMAT:
 *  <top of file>
 *      number of documents
 *      number of unique words
 *      number of non-zero words in the collection
 *      docID wordID count
 *      docID wordID count
 *      ...
 *  <end of file>
 */
float** loadDocFile(const char *fname, int &dc, int &wc)
{
    ifstream infile(fname);
    
    // get the number of documents and words in the data set
    int nzwc; // non-zero word count (ignored)
    infile >> dc >> wc >> nzwc;

    // set up the matrix and initialize it to all zeros
    float **mat = new float*[dc];
    for(int i=0; i<dc; i++) {
        mat[i] = new float[wc];
        memset(mat[i], 0, wc * sizeof(*mat[i]));
    }

    // populate the matrix from the data file
    string line;
    while(getline(infile, line)) {
        istringstream iss(line);
        int doc_id, word_id, count;
        if(!(iss >> doc_id >> word_id >> count))
            continue;
        mat[doc_id-1][word_id-1] = count;
    }

    infile.close();
    return mat;
}



// Read the word data into a list. Words are just organized one word per line.
char** loadWordsFile(const char *fname, int wc)
{
    ifstream infile(fname);
    char **words = new char*[wc];

    int i = 0;
    string line;
    while(getline(infile, line)) {
        // if we're out of word space and the file has more, stop
        if(i >= wc)
            break;

        // copy the word into memory
        char *word = new char[line.size() + 1];
        word[line.size()] = 0;
        memcpy(word, line.c_str(), line.size());
        words[i] = word;
        i++;
    }

    infile.close();
    return words;
}



// Takes argc and argv from program input and parses the parameters to set
// values for k (number of clusters) and num_threads (the maximum number
// of threads for Galois to use).
// Returns -1 on fail (provided file doesn't exist), else 0 on success.
int processArgs(int argc, char **argv,
    string *fname, unsigned int *k, unsigned int *num_threads)
{
    // set up file name
    if(argc >= 2)
        *fname = string(argv[1]);
    else
        *fname = "data";

    // check that the file exists - if not, error
    ifstream test(fname->c_str());
    if(!test.good()) {
        cout << "Error: file \"" << *fname << "\" does not exist." << endl;
        test.close();
        return -1;
    }
    test.close();

    // set up size of k
    if(argc >= 3)
        *k = atoi(argv[2]);
    else
        *k = DEFAULT_K;

    // set up number of threads
    if(argc >= 4) 
        *num_threads = atoi(argv[3]);
    else
        *num_threads = DEFAULT_THREADS;

    return 0;
}



// main: set up Galois and start the clustering process.
int main(int argc, char **argv)
{

    // get file name, and set up k and number of threads
    string fname;
    unsigned int k, num_threads;
    if(processArgs(argc, argv, &fname, &k, &num_threads) != 0)
        return -1;

    // tell Galois the max thread count
    Galois::setActiveThreads(num_threads);
    num_threads = Galois::getActiveThreads();
    cout << "Running SPK Means on \"" << fname << "\" with k=" << k
         << " (" << num_threads << " threads)." << endl;

    // set up the sparse matrix
    int dc, wc;
    float **D = loadDocFile(fname.c_str(), dc, wc);

    // run spherical k-means on the given sparse matrix D
    Results r = runSPKMeans(D, k, dc, wc);

    char **words = loadWordsFile("../TestData/vocabulary", r.wc);
    displayResults(&r, words, 10);

    r.clearMemory(); // TODO - destructor gets called anyway

    return 0;
}
