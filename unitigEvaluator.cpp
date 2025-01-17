#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include "sparsepp/spp.h"
#include <omp.h>



using spp::sparse_hash_map;



uint64_t xs(uint64_t y){
	y^=(y<<13); y^=(y>>17);y=(y^=(y<<15)); return y;
}



using namespace std;



string intToString(uint64_t n){
	if(n<1000){
		return to_string(n);
	}
	string end(to_string(n%1000));
	if(end.size()==3){
		return intToString(n/1000)+","+end;
	}
	if(end.size()==2){
		return intToString(n/1000)+",0"+end;
	}
	return intToString(n/1000)+",00"+end;
}



char revCompChar(char c) {
	switch (c) {
		case 'A': return 'T';
		case 'C': return 'G';
		case 'G': return 'C';
	}
	return 'A';
}



string revComp(const string& s){
	string rc(s.size(),0);
	for (int i((int)s.length() - 1); i >= 0; i--){
		rc[s.size()-1-i] = revCompChar(s[i]);
	}
	return rc;
}



string getCanonical(const string& str){
	return (min(str,revComp(str)));
}



uint64_t str2num(const string& str){
	uint64_t res(0);
	for(uint64_t i(0);i<str.size();i++){
		res<<=2;
		switch (str[i]){
			case 'A':res+=0;break;
			case 'C':res+=1;break;
			case 'G':res+=2;break;
			default:res+=3;break;
		}
	}
	return res;
}



int main(int argc, char ** argv){
	if(argc<4){
		cout<<"[unitig file] [reference file] [k value] [core number] [n for 2^n pass]"<<endl;
		exit(0);
	}
	auto start = chrono::system_clock::now();
	string inputUnitig(argv[1]);
	string inputRef(argv[2]);
	unsigned int k(stoi(argv[3]));
	unsigned int n(0);
	unsigned int nb_cores(stoi(argv[4]));
	if(argc>5){
		 n=(stoi(argv[5]));
	}
	unsigned int nbHash=1<<n;
	cout<<"I will perform "<<nbHash<<" pass"<<endl;
	srand (time(NULL));

	ifstream inRef(inputRef),inUnitigs(inputUnitig);
	if(not inRef.good() or not inUnitigs.good()){
		cout<<"Problem with files opening"<<endl;
		exit(1);
	}
	uint64_t FP(0),TP(0),FN(0),size(0),number(0),genomicKmersNum(0);
	omp_lock_t lock[1024];
	for (int i=0; i<1024; i++)
        omp_init_lock(&(lock[i]));

	for(unsigned int HASH(0);HASH<nbHash;++HASH){
		vector<sparse_hash_map<string, bool>> genomicKmers;
		genomicKmers.resize(1024);

		#pragma omp parallel num_threads(nb_cores)
		{
			string ref, useless,canon;
			while(not inRef.eof()){
				#pragma omp critical(dataupdate)
				{
					getline(inRef,useless);
					getline(inRef,ref);
				}
				if(not ref.empty() and not useless.empty()){
					for(unsigned int i(0);i+k<=ref.size();++i){

						canon=(getCanonical(ref.substr(i,k)));
						//~ cerr<<canon<<endl;
						uint64_t num((str2num(canon)));
						if(num%nbHash==HASH){
							uint64_t num2( (num/nbHash)%1024);
							omp_set_lock(&(lock[num2]));
							genomicKmers[num2][canon]=false;
							omp_unset_lock(&(lock[num2]));
							#pragma omp atomic
							genomicKmersNum++;
						}
					}
				}
				useless=ref="";
			}
		}

		#pragma omp parallel num_threads(nb_cores)
		{
			string ref, useless,canon;
			while(not inUnitigs.eof()){
				#pragma omp critical(dataupdate)
				{
					getline(inUnitigs,useless);
					getline(inUnitigs,ref);
				}
				if(not ref.empty() and not useless.empty()){
					#pragma omp atomic
					size+=ref.size();
					#pragma omp atomic
					number++;
					for(unsigned int i(0);i+k<=ref.size();++i){
						canon=getCanonical(ref.substr(i,k));
						uint64_t num((str2num(canon)));
						if(num%nbHash==HASH){
							if(genomicKmers[(num/nbHash)%1024].count(canon)==0){
								#pragma omp atomic
								FP++;
							}else{
								#pragma omp atomic
								TP++;
							}
						}
					}
				}
			}
		}
		if(HASH==0){
			cout<<"Unitig number: "<<intToString(number)<< " Total size: "<<intToString(size)<<" Mean: "<<intToString(size/number)<<endl;
			cout<<"Genomic kmer in the reference: "<<intToString(genomicKmersNum)<<endl;
		}
		FN=genomicKmersNum-TP;
		if(HASH!=nbHash-1){
			cout<<"PARTIAL RESULTS:"<<endl;
			cout<<"True positive (kmers in the unitig and the references) 		GOOD kmers:	"<<intToString(TP)<<endl;
			cout<<"False positive (kmers in the unitig and NOT in the references)	ERRONEOUS kmers:	"<<intToString(FP)<<endl;
			cout<<"False Negative (kmers NOT in the unitig but in the references)	MISSING kmers:	"<<intToString(FN)<<endl;
			cout<<"Erroneous kmer rate (*10,000): "<<(double)10000*FP/(FP+TP)<<endl;
			cout<<"Missing kmer rate (*10,000): "<<(double)10000*FN/genomicKmersNum<<endl;
		}
		inUnitigs.clear();
		inUnitigs.seekg(0, std::ios::beg);
		inRef.clear();
		inRef.seekg(0, std::ios::beg);
	}
	cout<<endl<<"FINAL RESULTS:"<<endl;
	cout<<genomicKmersNum<<" "<<TP<<endl;
	FN=genomicKmersNum-TP;

	cout<<"True positive (kmers in the unitig and the references) 		GOOD kmers:	"<<intToString(TP)<<endl;
	cout<<"False positive (kmers in the unitig and NOT in the references)	ERRONEOUS kmers:	"<<intToString(FP)<<endl;
	cout<<"False Negative (kmers NOT in the unitig but in the references)	MISSING kmers:	"<<intToString(FN)<<endl;
	cout<<"Recall: ";
	cout<<(double)(100*TP)/(TP+FN)<<endl;
	cout<<"Precision: ";
	cout<<(double)(100*TP)/(TP+FP)<<endl;
	cout<<"F1-score: ";
	cout<<(double)(100*TP*2)/(2*TP+FN+FP)<<endl;
	auto end = chrono::system_clock::now();
	chrono::duration<double> elapsed_seconds = end - start;
    time_t end_time = chrono::system_clock::to_time_t(end);

    cout << "\nFinished computation at " << ctime(&end_time)<< "Elapsed time: " << elapsed_seconds.count() << "s\n";

}
