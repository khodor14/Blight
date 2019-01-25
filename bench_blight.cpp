#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iterator>
#include <ctime>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <iterator>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "blight.h"



using namespace std;
using namespace chrono;




int main(int argc, char ** argv){
	char ch;
	string input,query;
	uint k(31);
	uint m1(9);
	uint m2(0);
	uint m3(3);
	uint c(1);
	uint bit(6);
	uint ex(0);
	while ((ch = getopt (argc, argv, "g:q:k:m:n:s:t:b:e:")) != -1){
		switch(ch){
			case 'q':
				query=optarg;
				break;
			case 'g':
				input=optarg;
				break;
			case 'k':
				k=stoi(optarg);
				break;
			case 'm':
				m1=stoi(optarg);
				break;
			case 'n':
				m2=stoi(optarg);
				break;
			case 's':
				m3=stoi(optarg);
				break;
			case 't':
				c=stoi(optarg);
				break;
			case 'e':
				ex=stoi(optarg);
				break;
			case 'b':
				bit=stoi(optarg);
				break;
		}
	}

	if(query=="" and input!=""){
		query=input;
	}
	if(input=="" and query!=""){
		input=query;
	}

	if(query=="" or input=="" or k==0){
		cout
		<<"Mandatory arguments"<<endl
		<<"-g graph file"<<endl
		<<"-q query file"<<endl
		<<"-k k value used for graph (31) "<<endl<<endl
		<<"-m minimizer size (9)"<<endl
		<<"-n to create 4^n mphf (m). More mean slower construction but better index, must be <=m"<<endl
		<<"-s to use 4^s files (3). More reduce memory usage and use more files, must be <=n"<<endl
		<<"-t core used (1)"<<endl
		<<"-b bit saved to encode positions (6). Will reduce the memory usage of b bit per kmer but query have to check 2^b kmers"<<endl;
		return 0;
	}
	{
		kmer_Set_Light ksl(k,m1,m2,m3,c,bit,ex);
		ksl.construct_index(input);

		ksl.file_query(query,false);

		//~ ksl.file_query(query,true);
		//~ high_resolution_clock::time_point t4 = high_resolution_clock::now();
		//~ duration<double> time_span3 = duration_cast<duration<double>>(t4 - t3);
		//~ cout << "The optimized query took me " << time_span3.count() << " seconds."<<endl;

		cout<<"I am glad you are here with me. Here at the end of all things."<<endl;
		//~ cout<<ksl.bucketSeq.size()<<endl;
		//~ cout<<ksl.positions.size()<<endl;
	}
	//~ cin.get();
	return 0;
}
