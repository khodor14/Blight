#include <stdio.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <atomic>
#include <mutex>
#include <stdint.h>
#include <unordered_map>
#include <map>
#include <pthread.h>
#include <chrono>
#include <omp.h>
#include <tmmintrin.h>
#include <math.h>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include "bbhash.h"
#include "blight.h"
#include "utils.h"
#include "zstr.hpp"
#include "common.h"
#include "robin_hood.h"


using namespace std;
using namespace chrono;



uint kmer_number_check(0);
uint kmer_number_check2(0);

uint kmer_number_check3(0);
uint skmer_number_check(0);



inline __uint128_t kmer_Set_Light::rcb(const __uint128_t& in){

	assume(k <= 64, "k=%u > 64", k);

	union kmer_u { __uint128_t k; __m128i m128i; uint64_t u64[2]; uint8_t u8[16];};

	kmer_u res = { .k = in };

	static_assert(sizeof(res) == sizeof(__uint128_t), "kmer sizeof mismatch");

	// Swap byte order

	kmer_u shuffidunrevhash = { .u8 = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0} };

	res.m128i = _mm_shuffle_epi8 (res.m128i, shuffidunrevhash.m128i);

	// Swap nuc order in bytes

	const uint64_t c1 = 0x0f0f0f0f0f0f0f0f;

	const uint64_t c2 = 0x3333333333333333;

	for(uint64_t& x : res.u64) {

		x = ((x & c1) << 4) | ((x & (c1 << 4)) >> 4); // swap 2-nuc order in bytes

		x = ((x & c2) << 2) | ((x & (c2 << 2)) >> 2); // swap nuc order in 2-nuc

		x ^= 0xaaaaaaaaaaaaaaaa; // Complement;

	}

	// Realign to the right

	res.m128i = mm_bitshift_right(res.m128i, 128 - 2*k);

	return res.k;
}



inline uint64_t  kmer_Set_Light::rcb(const uint64_t& in){
    assume(k <= 32, "k=%u > 32", k);
    // Complement, swap byte order
    uint64_t res = __builtin_bswap64(in ^ 0xaaaaaaaaaaaaaaaa);
    // Swap nuc order in bytes
    const uint64_t c1 = 0x0f0f0f0f0f0f0f0f;
    const uint64_t c2 = 0x3333333333333333;
    res               = ((res & c1) << 4) | ((res & (c1 << 4)) >> 4); // swap 2-nuc order in bytes
    res               = ((res & c2) << 2) | ((res & (c2 << 2)) >> 2); // swap nuc order in 2-nuc

    // Realign to the right
    res >>= 64 - 2 * k;
    return res;
}



uint64_t kmer_Set_Light::canonize(uint64_t x,uint64_t n){
	return min(x,rcbc(x,n));
}



inline void kmer_Set_Light::updateK(kmer& min, char nuc){
	min<<=2;
	min+=nuc2int(nuc);
	min%=offsetUpdateAnchor;
}



inline void kmer_Set_Light::updateM(kmer& min, char nuc){
	min<<=2;
	min+=nuc2int(nuc);
	min%=offsetUpdateMinimizer;
}



inline void kmer_Set_Light::updateRCK(kmer& min, char nuc){
	min>>=2;
	min+=(nuc2intrc(nuc)<<(2*k-2));
}



inline void kmer_Set_Light::updateRCM(kmer& min, char nuc){
	min>>=2;
	min+=(nuc2intrc(nuc)<<(2*minimizer_size_graph-2));
}



static inline kmer get_int_in_kmer(kmer seq,uint64_t pos,uint64_t number_nuc){
	seq>>=2*pos;
	return  ((seq)%(1<<(2*number_nuc)));
}



kmer kmer_Set_Light::regular_minimizer(kmer seq){
	kmer mini,mmer;
	mmer=seq%minimizer_number_graph;
	mini=mmer=canonize(mmer,minimizer_size_graph);
	uint64_t hash_mini = (unrevhash(mmer));
	for(uint64_t i(1);i<=k-minimizer_size_graph;i++){
		seq>>=2;
		mmer=seq%minimizer_number_graph;
		mmer=canonize(mmer,minimizer_size_graph);
		uint64_t hash = (unrevhash(mmer));
		if(hash_mini>hash){
			mini=mmer;
			hash_mini=hash;
		}
	}
	return revhash((uint64_t)mini)%minimizer_number;
}



kmer kmer_Set_Light::regular_minimizer_pos(kmer seq,uint64_t& position){
	kmer mini,mmer;
	mmer=seq%minimizer_number_graph;
	mini=mmer=canonize(mmer,minimizer_size_graph);
	uint64_t hash_mini = (unrevhash(mmer));
	position=0;
	for(uint64_t i(1);i<=k-minimizer_size_graph;i++){
		seq>>=2;
		mmer=seq%minimizer_number_graph;
		mmer=canonize(mmer,minimizer_size_graph);
		uint64_t hash = (unrevhash(mmer));
		if(hash_mini>hash){
			position=k-minimizer_size_graph-i;
			mini=mmer;
			hash_mini=hash;
		}
	}
	return mini;
}


uint16_t kmer_Set_Light::abundance_at (uint8_t index)
{
	if (index < abundance_discretization.size())
	{
		return floorf((abundance_discretization[index]  +  abundance_discretization[index+1])/2.0);
	}
	else
	{
		return 0;
	}
}



void kmer_Set_Light::init_discretization_scheme()
{
	MAX_ABUNDANCE_DISCRETE=65536;
	abundance_discretization.resize(257);
	int total =0;
	abundance_discretization[0] = 0;
	int idx=1;
	for(int ii=1; ii<= 70; ii++,idx++ )
	{
		total += 1;
		abundance_discretization[idx] = total ;
	}

	for(int ii=1; ii<= 15; ii++,idx++ )
	{
		total += 2;
		abundance_discretization[idx] = total  ;
	}

	for(int ii=1; ii<= 40; ii++,idx++ )
	{
		total += 10;
		abundance_discretization[idx] = total  ;
	}
	for(int ii=1; ii<= 25; ii++,idx++ )
	{
		total += 20;
		abundance_discretization[idx] = total  ;
	}
	for(int ii=1; ii<= 40; ii++,idx++ )
	{
		total += 100;
		abundance_discretization[idx] = total  ;
	}
	for(int ii=1; ii<= 25; ii++,idx++ )
	{
		total += 200;
		abundance_discretization[idx] = total  ;
	}
	for(int ii=1; ii<= 40; ii++,idx++ )
	{
		total += 1000;
		abundance_discretization[idx] = total  ;
	}
	abundance_discretization[256] = total;
}


uint8_t kmer_Set_Light::return_count_bin(uint16_t abundance)
{
	int idx ;
	if (abundance >= MAX_ABUNDANCE_DISCRETE)
	{
		//~ _nb_abundances_above_precision++;
		//std::cout << "found abundance larger than discrete: " << abundance << std::endl;
		idx = abundance_discretization.size() -2 ;
	}
	else
	{
		//get first cell strictly greater than abundance
		auto  up = upper_bound(abundance_discretization.begin(), abundance_discretization.end(), abundance);
		up--; // get previous cell
		idx = up- abundance_discretization.begin() ;
	}
	return idx;
}




uint16_t kmer_Set_Light::parseCoverage_bin(const string& str){
	size_t pos(str.find("km:f:"));
	if(pos==string::npos){
		pos=(str.find("KM:f:"));
	}
	if(pos==string::npos){
		return 1;
	}
	uint i(1);
	while(str[i+pos+5]!=' '){
		++i;
	}
	// cout<<"bin"<<(int) return_count_bin((uint16_t)stof(str.substr(pos+5,i)))<<endl;cin.get ();
	return return_count_bin((uint16_t)stof(str.substr(pos+5,i)));
}



void kmer_Set_Light::construct_index(const string& input_file, const string& tmp_dir){
	if(not tmp_dir.empty()){
		working_dir=tmp_dir+"/";
	}
	if(m1<m2){
		cout<<"n should be inferior to m"<<endl;
		exit(0);
	}
	if(m2<m3){
		cout<<"s should be inferior to n"<<endl;
		exit(0);
	}

	high_resolution_clock::time_point t1 = high_resolution_clock::now();

	nuc_minimizer=new uint32_t[minimizer_number.value()];
	current_pos=new uint64_t[minimizer_number.value()];
	start_bucket=new uint64_t[minimizer_number.value()];
	create_super_buckets(input_file);

	high_resolution_clock::time_point t12 = high_resolution_clock::now();
	duration<double> time_span12 = duration_cast<duration<double>>(t12 - t1);
	cout<<"Super bucket created: "<< time_span12.count() << " seconds."<<endl;

	read_super_buckets(working_dir+"_blout");

	high_resolution_clock::time_point t13 = high_resolution_clock::now();
	duration<double> time_span13 = duration_cast<duration<double>>(t13 - t12);
	cout<<"Indexes created: "<< time_span13.count() << " seconds."<<endl;
	duration<double> time_spant = duration_cast<duration<double>>(t13 - t1);
	cout << "The whole indexing took me " << time_spant.count() << " seconds."<< endl;
	delete [] nuc_minimizer;
	delete [] start_bucket;
	delete [] current_pos;
}



void kmer_Set_Light::reset(){
	number_kmer=number_super_kmer=largest_MPHF=positions_total_size=positions_int=total_nb_minitigs=largest_bucket_nuc_all=0;
	for(uint64_t i(0);i<mphf_number;++i){
		all_mphf[i].mphf_size=0;
		all_mphf[i].bit_to_encode=0;
		all_mphf[i].start=0;
		all_mphf[i].empty=true;

	}
	for(uint64_t i(0);i<minimizer_number.value();++i){
		//~ all_buckets[i].start=0;
		nuc_minimizer[i]=0;
		start_bucket[i]=0;
		current_pos[i]=0;
		all_buckets[i].skmer_number=0;
	}
}






bool equal_nonull(const vector<uint16_t>& V1,const vector<uint16_t>& V2){
	//~ return true;
	if(V1.empty()){return false;}
	if(V2.size()!=V1.size()){return false;}
	for(uint i(0);i<V1.size();++i){
		if((V1[i]==0)^(V2[i]==0)){
			return false;
		}
	}
	return true;
}



bool kmer_Set_Light::similar_count(const vector<uint16_t>& V1,const vector<uint16_t>& V2){
	if(V1.empty()){return false;}
	if(V2.size()!=V1.size()){return false;}
	for(uint i(0);i<V1.size();++i){
		if( ((double)max(V1[i],V2[i])/((double)min(V1[i],V2[i])) >max_divergence_count)){
			return false;
		}
	}
	return true;
}



uint32_t misscompaction(0);



string kmer_Set_Light::compaction(const string& seq1,const string& seq2, bool recur=true){
	uint s1(seq1.size()),s2(seq2.size());
	if(s1==0 or s2==0){return "";}
	string rc2(revComp(seq2)),end1(seq1.substr(s1-k+1,k-1)), beg2(seq2.substr(0,k-1));
	if(end1==beg2){return seq1+(seq2.substr(k-1));}
	string begrc2(rc2.substr(0,k-1));
	if(end1==begrc2){return seq1+(rc2.substr(k-1));}
	if(recur){
		return compaction(revComp(seq1),seq2,false);
	}else{
	}
	return "";
}



void kmer_Set_Light::construct_index_fof(const string& input_file, const string& tmp_dir, int colormode, double max_divergence){
	omp_set_nested(2);
	if(not tmp_dir.empty()){
		working_dir=tmp_dir+"/";
	}
	color_mode=colormode;
	if(color_mode==1){
		max_divergence_count=(max_divergence/100)+1;
	}
	if(color_mode==2){
		init_discretization_scheme();
	}
	if(m1<m2){
		cout<<"n should be inferior to m"<<endl;
		exit(0);
	}
	if(m2<m3){
		cout<<"s should be inferior to n"<<endl;
		exit(0);
	}
	nuc_minimizer=new uint32_t[minimizer_number.value()];
	current_pos=new uint64_t[minimizer_number.value()];
	start_bucket=new uint64_t[minimizer_number.value()];
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	ifstream infof(input_file);
	vector<string> fnames;
	//STACK SUPERBUCKETS
	while(not infof.eof()){
		string file;
		getline(infof,file);
		if(not file.empty() and exists_test(file)){
			fnames.push_back(file);
		}
	}

	create_super_buckets_list(fnames);
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	cout<<"Partition created	"<<intToString(read_kmer)<<" kmers read "<<endl;
	duration<double> time_span12 = duration_cast<duration<double>>(t2 - t1);
	cout<<time_span12.count() << " seconds."<<endl;
	{
		ofstream out(working_dir+"_blmonocolor.fa",ios::app);
		//~ #pragma omp parallel for num_threads(min(coreNumber,(uint64_t)4))
		//~ #pragma omp parallel for num_threads((coreNumber)) schedule(static)
		for(uint i_superbuckets=0; i_superbuckets<number_superbuckets.value(); ++i_superbuckets){
			merge_super_buckets_mem(working_dir+"_blout"+to_string(i_superbuckets),fnames.size(),&out);
			remove((working_dir+"_blsout"+to_string(i_superbuckets)).c_str());
			cout<<"-"<<flush;
		}
	}
	cout<<endl;
	reset();


	high_resolution_clock::time_point t3 = high_resolution_clock::now();
	cout<<"Monocolor minitig computed, now regular indexing start"<<endl;
	duration<double> time_span32 = duration_cast<duration<double>>(t3 - t2);
	cout<<time_span32.count() << " seconds."<<endl;


	create_super_buckets(working_dir+"_blmonocolor.fa");

	high_resolution_clock::time_point t4 = high_resolution_clock::now();
	duration<double> time_span43 = duration_cast<duration<double>>(t4 - t3);
	cout<<"Super buckets created: "<< time_span43.count() << " seconds."<<endl;

	read_super_buckets(working_dir+"_blout");
	delete [] nuc_minimizer ;
	delete [] start_bucket ;
	delete [] current_pos ;

	high_resolution_clock::time_point t5 = high_resolution_clock::now();
	duration<double> time_span53 = duration_cast<duration<double>>(t5 - t3);
	cout<<"Indexes created: "<< time_span53.count() << " seconds."<<endl;
	duration<double> time_spant = duration_cast<duration<double>>(t5 - t1);
	cout << "The whole indexing took me " << time_spant.count() << " seconds."<< endl;
}



void kmer_Set_Light::merge_super_buckets_mem(const string& input_file, uint64_t number_color, ofstream* out){
	vector<uint16_t> bit_vector(number_color,0);
	uint number_pass(8);
	for(uint pass(0);pass<number_pass;++pass){
		vector<robin_hood::unordered_map<kmer,kmer_context>> min2kmer2context(minimizer_number.value()/number_superbuckets.value());
		uint64_t mms=min2kmer2context.size();
		vector<int32_t> minimizers(mms,-1);
		zstr::ifstream in(input_file);
		#pragma omp parallel num_threads(coreNumber)
		{
			vector<string> buffer;
			string line;
			vector<string> splitted;
			while(not in.eof() and in.good()){
				#pragma omp critical
				{
					for(uint i(0);i<100 and not in.eof() ;++i){
						getline(in,line);
						buffer.push_back(line);
					}
				}
				uint64_t bsize(buffer.size());
				for(uint i(0);i<bsize;++i){
					line=buffer[i];
					if(line.empty()){break;}
					split(line,':',splitted);
					minitig mini;
					int32_t color=stoi(splitted[1]);
					string  sequence=(splitted[2]);
					uint16_t coverage=(stoi(splitted[3]));
					uint64_t min_int=(stoi(splitted[0]));
					uint64_t indice(min_int%mms);
					if((indice%number_pass)!=pass){continue;}
					positions_mutex[indice%4096].lock();
					kmer seq(str2num(sequence.substr(0,k))),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq)),prev;
					canon=(min_k(seq, rcSeq));
					// cout<<sequence.substr(0,k)<<"	"<<color<<"	"<<coverage<<"	"<<min_int<<endl;cin.get();
					if(min2kmer2context[indice].count(canon)==0){
						min2kmer2context[indice][canon]={false,bit_vector};
					}
					min2kmer2context[indice][canon].count[color] = coverage;
					uint64_t sks=sequence.size();
					for(uint i(0);i+k<sks;++i){
						prev=canon;
						updateK(seq,sequence[i+k]);
						updateRCK(rcSeq,sequence[i+k]);
						canon=(min_k(seq, rcSeq));
						if(min2kmer2context[indice].count(canon)==0){
							min2kmer2context[indice][canon]={false,bit_vector};
						}
						min2kmer2context[indice][canon].count[color] = coverage;
					}
					positions_mutex[indice%4096].unlock();
					minimizers[min_int%mms]=(min_int);
				}
			}
		}
		get_monocolor_minitigs_mem(min2kmer2context,out,minimizers,number_color);
	}
}



string nucleotides("ACGT");



kmer kmer_Set_Light::select_good_successor(const  robin_hood::unordered_map<kmer,kmer_context>& kmer2context,const kmer& start){
	kmer canon=canonize(start,k);
	if(kmer2context.count(canon)==0){return -1;}
	kmer_context kc(kmer2context.at(canon));
	for(uint64_t i(0);i<4;++i){
		kmer target=start;
		updateK(target,nucleotides[i]);
		kmer targetc=canonize(target,k);
		if(kmer2context.count(targetc)!=0){
			if(kmer2context.at(targetc).isdump){continue;}
			// if(color_mode!=0){
			if(kmer2context.at(targetc).count==kc.count){
				return target;
			}
		}
	}
	return -1;
}



void kmer_Set_Light::get_monocolor_minitigs_mem(vector<robin_hood::unordered_map<kmer,kmer_context>>&  min2kmer2context , ofstream* out, const vector<int32_t>& mini,uint64_t number_color){
vector<uint16_t> bit_vector(number_color,0);
#pragma omp parallel num_threads(coreNumber)
{
	string sequence, buffer,seq2dump,compact;
	uint64_t ms=min2kmer2context.size();
	#pragma omp for schedule(static,ms/coreNumber)
	for(uint i_set=(0);i_set<ms;i_set++){
		for (auto& it: min2kmer2context[i_set]){
			if(not it.second.isdump){
				it.second.isdump=true;
				auto colorV2dump(it.second.count);
				kmer start=it.first;
				seq2dump=kmer2str(start);
				for(uint64_t step(0);step<2;step++){
					while(true){
						kmer next=select_good_successor(min2kmer2context[i_set],start);
						if(next==(kmer)-1){break;}
						compact=compaction(seq2dump,kmer2str(next),false);
						if(compact.empty()){break;}
						min2kmer2context[i_set].at(canonize(next,k)).isdump=true;
						seq2dump=compact;
						start=next;
					}
					start=rcb(it.first);
					seq2dump=revComp(seq2dump);
				}
				buffer+=">"+to_string(mini[i_set])+color_coverage2str(colorV2dump)+"\n"+seq2dump+"\n";
				if(buffer.size()>8000){
					#pragma omp critical (monocolorFile)
					{
						*out<<buffer;
					}
					buffer.clear();
				}
			}
		}
		min2kmer2context[i_set].clear();
	}
	#pragma omp critical (monocolorFile)
	{
		*out<<buffer;
	}
	buffer.clear();
}
}



uint16_t kmer_Set_Light::parseCoverage(const string& str){
	if(color_mode==0){return parseCoverage_bool(str);}
	if(color_mode==1){return parseCoverage_exact(str);}
	if(color_mode==2){return parseCoverage_bin(str);}
	return parseCoverage_log2(str);
}


void kmer_Set_Light::create_super_buckets_list(const vector<string>& input_files){
	struct rlimit rl;
	getrlimit (RLIMIT_NOFILE, &rl);
	rl.rlim_cur = number_superbuckets.value()+10+coreNumber;
	setrlimit (RLIMIT_NOFILE, &rl);
	// atomic<uint64_t> total_nuc_number(0);

	vector<ostream*> out_files;
	for(uint64_t i(0);i<number_superbuckets;++i){
		auto out =new  ofstream(working_dir+"_blout"+to_string(i),ofstream::app);
		out_files.push_back(out);
	}
	omp_lock_t lock[number_superbuckets.value()];
	for (uint64_t i=0; i<number_superbuckets; i++){
		omp_init_lock(&(lock[i]));
	}

	#pragma omp parallel for num_threads(coreNumber)
	for(uint64_t i_file=0;i_file<input_files.size();++i_file){
		auto inUnitigsread=new zstr::ifstream(input_files[i_file]);
		if(not inUnitigsread->good()){
			cout<<"Problem with files opening"<<endl;
			continue;
		}

		string ref,useless;
		vector<string> buffer(number_superbuckets.value());
		minimizer_type old_minimizer,minimizer;
		while(not inUnitigsread->eof()){
			ref=useless="";
			getline(*inUnitigsread,useless);
			getline(*inUnitigsread,ref);
			if(ref.size()<k){
				ref="";
			}else{
				#pragma omp atomic
				read_kmer+=ref.size()-k+1;
			}
			//FOREACH UNITIG
			if(not ref.empty() and not useless.empty()){
				old_minimizer=minimizer=minimizer_number.value();
				uint64_t last_position(0);
				//FOREACH KMER
				kmer seq(str2num(ref.substr(0,k)));
				uint64_t position_min;
				uint64_t min_seq=(str2num(ref.substr(k-minimizer_size_graph,minimizer_size_graph))),min_rcseq(rcbc(min_seq,minimizer_size_graph)),min_canon(min(min_seq,min_rcseq));
				minimizer=regular_minimizer_pos(seq,position_min);
				old_minimizer=minimizer;
				uint64_t hash_min=unrevhash(minimizer);
				uint64_t i(0);
				for(;i+k<ref.size();++i){
					updateK(seq,ref[i+k]);
					updateM(min_seq,ref[i+k]);
					updateRCM(min_rcseq,ref[i+k]);
					min_canon=(min(min_seq,min_rcseq));
					uint64_t new_h=unrevhash(min_canon);
						//THE NEW mmer is a MINIMIZor
					if(new_h<hash_min){
						minimizer=(min_canon);
						hash_min=new_h;
						position_min=i+k-minimizer_size_graph+1;
					}else{
						//the previous minimizer is outdated
						if(i>=position_min){
							minimizer=regular_minimizer_pos(seq,position_min);
							hash_min=unrevhash(minimizer);
							position_min+=(i+1);
						}else{
						}
					}
					if(old_minimizer!=minimizer){
						old_minimizer=(revhash(old_minimizer)%minimizer_number);
						buffer[old_minimizer/bucket_per_superBuckets.value()]+=to_string(old_minimizer)+":"+to_string(i_file)+":"+ref.substr(last_position,i-last_position+k)+":"+to_string(parseCoverage(useless))+"\n";
						if(buffer[old_minimizer/bucket_per_superBuckets.value()].size()>80000){
							omp_set_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
							*(out_files[((old_minimizer))/bucket_per_superBuckets.value()])<<buffer[old_minimizer/bucket_per_superBuckets.value()];
							omp_unset_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
							buffer[old_minimizer/bucket_per_superBuckets.value()].clear();
						}
						last_position=i+1;
						old_minimizer=minimizer;
					}
				}
				if(ref.size()-last_position>k-1){
					old_minimizer=(revhash(old_minimizer)%minimizer_number);
					buffer[old_minimizer/bucket_per_superBuckets.value()]+=to_string(old_minimizer)+":"+to_string(i_file)+":"+ref.substr(last_position)+":"+to_string(parseCoverage(useless))+"\n";
					if(buffer[old_minimizer/bucket_per_superBuckets.value()].size()>80000){
						omp_set_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
						*(out_files[((old_minimizer))/bucket_per_superBuckets.value()])<<buffer[old_minimizer/bucket_per_superBuckets.value()];
						omp_unset_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
						buffer[old_minimizer/bucket_per_superBuckets.value()].clear();
					}
				}
			}
		}
		for(uint64_t i(0);i<number_superbuckets.value();++i){
			if(not buffer[i].empty()){
				omp_set_lock(&(lock[i]));
				*(out_files[i])<<buffer[i];
				omp_unset_lock(&(lock[i]));
			}
		}
		delete inUnitigsread;
	}

	for(uint64_t i(0);i<number_superbuckets;++i){
		*out_files[i]<<flush;
		delete(out_files[i]);
	}
}



void kmer_Set_Light::create_super_buckets(const string& input_file){
	struct rlimit rl;
	getrlimit (RLIMIT_NOFILE, &rl);
	rl.rlim_cur = number_superbuckets.value()+10;
	setrlimit (RLIMIT_NOFILE, &rl);
	uint64_t total_nuc_number(0);
	auto inUnitigs=new zstr::ifstream( input_file);
	if( not inUnitigs->good()){
		cout<<"Problem with files opening"<<endl;
		exit(1);
	}
	vector<ostream*> out_files;
	for(uint64_t i(0);i<number_superbuckets;++i){
		auto out =new ofstream(working_dir+"_blout"+to_string(i));
		if(not out->good()){
			cout<<"Problem with files opening"<<endl;
			exit(1);
		}
		out_files.push_back(out);
	}
	omp_lock_t lock[number_superbuckets.value()];
	for (uint64_t i=0; i<number_superbuckets; i++){
		omp_init_lock(&(lock[i]));
	}
	#pragma omp parallel num_threads(coreNumber)
	{
		string ref,useless;
		vector<string> buffer(number_superbuckets.value());
		minimizer_type old_minimizer,minimizer;
		while(not inUnitigs->eof()){
			ref=useless="";
			#pragma omp critical(dataupdate)
			{
				getline(*inUnitigs,useless);
				getline(*inUnitigs,ref);
				if(ref.size()<k){
					ref="";
				}else{
					read_kmer+=ref.size()-k+1;
				}
			}
			//FOREACH UNITIG
			if(not ref.empty() and not useless.empty()){
				old_minimizer=minimizer=minimizer_number.value();
				uint64_t last_position(0);
				//FOREACH KMER
				kmer seq(str2num(ref.substr(0,k)));
				uint64_t position_min;
				uint64_t min_seq=(str2num(ref.substr(k-minimizer_size_graph,minimizer_size_graph))),min_rcseq(rcbc(min_seq,minimizer_size_graph)),min_canon(min(min_seq,min_rcseq));
				minimizer=regular_minimizer_pos(seq,position_min);
				old_minimizer=minimizer;
				uint64_t hash_min=unrevhash(minimizer);
				uint64_t i(0);
				for(;i+k<ref.size();++i){
					updateK(seq,ref[i+k]);
					updateM(min_seq,ref[i+k]);
					updateRCM(min_rcseq,ref[i+k]);
					min_canon=(min(min_seq,min_rcseq));
					uint64_t new_h=unrevhash(min_canon);
						//THE NEW mmer is a MINIMIZor
					if(new_h<hash_min){
						minimizer=(min_canon);
						hash_min=new_h;
						position_min=i+k-minimizer_size_graph+1;
					}else{
						//the previous minimizer is outdated
						if(i>=position_min){
							minimizer=regular_minimizer_pos(seq,position_min);
							hash_min=unrevhash(minimizer);
							position_min+=(i+1);
						}else{
						}
					}
					//COMPUTE KMER MINIMIZER
					if(old_minimizer!=minimizer){
						old_minimizer=(revhash(old_minimizer)%minimizer_number);
						buffer[old_minimizer/bucket_per_superBuckets.value()]+=">"+to_string(old_minimizer)+"\n"+ref.substr(last_position,i-last_position+k)+"\n";
						// *(out_files[((old_minimizer))/bucket_per_superBuckets])<<">"+to_string(old_minimizer)+"\n"<<ref.substr(last_position,i-last_position+k)<<"\n";
						if(buffer[old_minimizer/bucket_per_superBuckets.value()].size()>80000){
							omp_set_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
							*(out_files[((old_minimizer))/bucket_per_superBuckets.value()])<<buffer[old_minimizer/bucket_per_superBuckets.value()];
							omp_unset_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
							buffer[old_minimizer/bucket_per_superBuckets.value()].clear();
						}
						#pragma omp atomic
						nuc_minimizer[old_minimizer]+=(i-last_position+k);
						#pragma omp atomic
						all_buckets[old_minimizer].skmer_number++;
						#pragma omp atomic
						all_mphf[old_minimizer/number_bucket_per_mphf].mphf_size+=(i-last_position+k)-k+1;
						all_mphf[old_minimizer/number_bucket_per_mphf].empty=false;
						#pragma omp atomic
						total_nuc_number+=(i-last_position+k);
						last_position=i+1;
						old_minimizer=minimizer;
					}
				}
				if(ref.size()-last_position>k-1){
					old_minimizer=(revhash(old_minimizer)%minimizer_number);
					buffer[old_minimizer/bucket_per_superBuckets.value()]+=">"+to_string(old_minimizer)+"\n"+ref.substr(last_position)+"\n";
					// *(out_files[((old_minimizer))/bucket_per_superBuckets])<<">"+to_string(old_minimizer)+"\n"<<ref.substr(last_position)<<"\n";
					if(buffer[old_minimizer/bucket_per_superBuckets.value()].size()>80000){
						omp_set_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
						*(out_files[((old_minimizer))/bucket_per_superBuckets.value()])<<buffer[old_minimizer/bucket_per_superBuckets.value()];
						omp_unset_lock(&(lock[((old_minimizer))/bucket_per_superBuckets.value()]));
						buffer[old_minimizer/bucket_per_superBuckets.value()].clear();
					}
					#pragma omp atomic
					nuc_minimizer[old_minimizer]+=(ref.substr(last_position)).size();
					#pragma omp atomic
					all_buckets[old_minimizer].skmer_number++;
					#pragma omp atomic
					total_nuc_number+=(ref.substr(last_position)).size();
					#pragma omp atomic
					all_mphf[old_minimizer/number_bucket_per_mphf].mphf_size+=(ref.substr(last_position)).size()-k+1;
					all_mphf[old_minimizer/number_bucket_per_mphf].empty=false;
				}
			}
		}
		for(uint64_t i(0);i<number_superbuckets.value();++i){
			if(not buffer[i].empty()){
				omp_set_lock(&(lock[i]));
				*(out_files[i])<<buffer[i];
				omp_unset_lock(&(lock[i]));
			}
		}
	}

	delete inUnitigs;
	for(uint64_t i(0);i<number_superbuckets;++i){
		*out_files[i]<<flush;
		delete(out_files[i]);
	}
	bucketSeq.resize(total_nuc_number*2);
	bucketSeq.shrink_to_fit();
	uint64_t i(0),total_pos_size(0);
	uint32_t max_bucket_mphf(0);
	uint64_t hash_base(0),old_hash_base(0),nb_skmer_before(0), last_skmer_number(0);
	for(uint64_t BC(0);BC<minimizer_number.value();++BC){
		start_bucket[BC]=i;
		current_pos[BC]=i;
		i+=nuc_minimizer[BC];
		max_bucket_mphf=max(all_buckets[BC].skmer_number,max_bucket_mphf);
		if (BC == 0){
			nb_skmer_before = 0; // I replace skmer_number by the total number of minitigs before this bucket
		}else{
			nb_skmer_before = all_buckets[BC-1].skmer_number;
		}
		uint64_t local_skmercount( all_buckets[BC].skmer_number);
		all_buckets[BC].skmer_number=total_nb_minitigs;
		total_nb_minitigs+=local_skmercount;
		if((BC+1)%number_bucket_per_mphf==0){
			int n_bits_to_encode((ceil(log2(max_bucket_mphf+1)))-bit_saved_sub);
			if(n_bits_to_encode<1){n_bits_to_encode=1;}
			all_mphf[BC/number_bucket_per_mphf].bit_to_encode=n_bits_to_encode;
			all_mphf[BC/number_bucket_per_mphf].start=total_pos_size;
			total_pos_size+=(n_bits_to_encode*all_mphf[BC/number_bucket_per_mphf].mphf_size);
			hash_base+=all_mphf[(BC/number_bucket_per_mphf)].mphf_size;
			all_mphf[BC/number_bucket_per_mphf].mphf_size=old_hash_base;
			old_hash_base=hash_base;
			max_bucket_mphf=0;
		}
	}
	total_nb_minitigs = all_buckets[(uint) minimizer_number - 1].skmer_number + last_skmer_number; // total number of minitigs
	positions.resize(total_pos_size);
	positions_int=positions.size()/64+(positions.size()%64==0 ? 0 :1);
	positions.shrink_to_fit();
}



void kmer_Set_Light::str2bool(const string& str,uint64_t mini){
	for(uint64_t i(0);i<str.size();++i){
		switch (str[i]){
			case 'A':
				bucketSeq[(current_pos[mini]+i)*2]=(false);
				bucketSeq[(current_pos[mini]+i)*2+1]=(false);
				break;
			case 'C':
				bucketSeq[(current_pos[mini]+i)*2]=(false);
				bucketSeq[(current_pos[mini]+i)*2+1]=(true);
				break;
			case 'G':
				bucketSeq[(current_pos[mini]+i)*2]=(true);
				bucketSeq[(current_pos[mini]+i)*2+1]=(true);
				break;
			default:
				bucketSeq[(current_pos[mini]+i)*2]=(true);
				bucketSeq[(current_pos[mini]+i)*2+1]=(false);
				break;
			}
	}
	current_pos[mini]+=(str.size());
}



void kmer_Set_Light::get_monocolor_minitigs(const  vector<string>& minitigs, const vector<int64_t>& color, const  vector<uint16_t>& coverage, ofstream* out, const string& mini,uint64_t number_color){
	unordered_map<kmer,kmer,KmerHasher> next_kmer;
	unordered_map<kmer,kmer,KmerHasher> previous_kmer;
	unordered_map<kmer,vector<uint16_t>,KmerHasher> kmer_color;
	vector<uint16_t> bit_vector(number_color,0);
	//ASSOCIATE INFO TO KMERS
	for(uint64_t i_mini(0);i_mini<minitigs.size();++i_mini){
		kmer seq(str2num(minitigs[i_mini].substr(0,k))),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq)),prev(-1);
		canon=(min_k(seq, rcSeq));
		if(kmer_color.count(canon)==0){
			kmer_color[canon]=bit_vector;
		}
		kmer_color[canon][color[i_mini]-1]=coverage[i_mini];
		for(uint i(0);i+k<minitigs[i_mini].size();++i){
			prev=canon;
			updateK(seq,minitigs[i_mini][i+k]);
			updateRCK(rcSeq,minitigs[i_mini][i+k]);
			canon=(min_k(seq, rcSeq));
			if(kmer_color.count(canon)==0){
				kmer_color[canon]=bit_vector;
			}
			kmer_color[canon][color[i_mini]-1]=coverage[i_mini];
			if(next_kmer.count(prev)==0){
				next_kmer[prev]=canon;
			}else{
				if(next_kmer[prev]!=canon and next_kmer[prev]!=(kmer)-1){
					next_kmer[prev]=(kmer)-1;
				}
			}
			if(previous_kmer.count(canon)==0){
				previous_kmer[canon]=prev;
			}else{
				if(previous_kmer[canon]!=prev and previous_kmer[canon]!=(kmer)-1){
					previous_kmer[canon]=(kmer)-1;
				}
			}
		}
	}
	string monocolor,compact;
	uint kmer_number_check(0);
	//FOREACH KMER COMPUTE MAXIMAL MONOCOLOR MINITIG
	for (auto& it: kmer_color) {
		kmer_number_check++;
		if(not it.second.empty()){
			auto dumpcolor(it.second);
			kmer canon=it.first;
			monocolor=kmer2str(canon);
			//GO FORWARD
			while(true){
				if(next_kmer.count(canon)==0){break;}
				if(next_kmer[canon]==(kmer)-1){break;}
				if(kmer_color[next_kmer[canon]]!=it.second){break;}
				kmer_color[next_kmer[canon]].clear();
				compact=compaction(monocolor,kmer2str(next_kmer[canon]));
				if(compact.empty()){break;}
				monocolor=compact;
				canon=next_kmer[canon];
			}
			//GO BACKWARD
			canon=it.first;
			while(true){
				if(previous_kmer.count(canon)==0){break;}
				if(previous_kmer[canon]==(kmer)-1){break;}
				if(kmer_color[previous_kmer[canon]]!=it.second){break;}
				kmer_color[previous_kmer[canon]].clear();
				compact=compaction(monocolor,kmer2str(previous_kmer[canon]));
				if(compact.empty()){break;}
				monocolor=compact;
				canon=previous_kmer[canon];
			}
			#pragma omp critical (monocolorFile)
			{
				*out<<">"+mini<<color_coverage2str(dumpcolor)<<"\n"<<monocolor<<"\n";
			}
		}
	}
}



void kmer_Set_Light::merge_super_buckets(const string& input_file, uint64_t number_color, ofstream* out){
	string line;
	vector<string> splitted;
	vector<string> minitigs;
	vector<int64_t> color;
	vector<uint16_t> coverage;
	zstr::ifstream in(input_file);
	int64_t minimizer=-1;
	while(not in.eof() and in.good()){
		getline(in,line);
		if(not line.empty()){
			splitted=split(line,':');
			if(stoi(splitted[0])>minimizer and not minitigs.empty()){
				get_monocolor_minitigs(minitigs,color,coverage,out,to_string(minimizer),number_color);
				minitigs.clear();
				color.clear();
				coverage.clear();
			}
			minimizer=stoi(splitted[0]);
			color.push_back(stoi(splitted[1]));
			minitigs.push_back(splitted[2]);
			coverage.push_back(stoi(splitted[3]));
		}
	}
	get_monocolor_minitigs(minitigs,color,coverage,out,to_string(minimizer),number_color);
}



void kmer_Set_Light::read_super_buckets(const string& input_file){
	//~ #pragma omp parallel num_threads(coreNumber)
	{
		string useless,line;
		//~ #pragma omp for
		for(uint64_t SBC=0;SBC<number_superbuckets.value();++SBC){
			vector<uint64_t> number_kmer_accu(bucket_per_superBuckets.value(),0);
			uint64_t BC(SBC*bucket_per_superBuckets);
			zstr::ifstream in((input_file+to_string(SBC)));
			while(not in.eof() and in.good()){
				useless=line="";
				getline(in,useless);
				getline(in,line);
				if(not line.empty()){
					useless=useless.substr(1);
					uint64_t minimizer(stoi(useless));
					str2bool(line,minimizer);
					//~ #pragma omp critical(PSK)
					{
						position_super_kmers[number_kmer_accu[minimizer%bucket_per_superBuckets]+all_mphf[minimizer].mphf_size]=true;
					}
					number_kmer+=line.size()-k+1;
					number_kmer_accu[minimizer%bucket_per_superBuckets]+=line.size()-k+1;
					line.clear();
					number_super_kmer++;
				}
			}
			remove((input_file+to_string(SBC)).c_str());
			//~ create_mphf_mem(BC,BC+bucket_per_superBuckets);
			create_mphf_disk(BC,BC+bucket_per_superBuckets);
			position_super_kmers.optimize();
			position_super_kmers.optimize_gap_size();
			fill_positions(BC,BC+bucket_per_superBuckets);
			BC+=bucket_per_superBuckets.value();
			cout<<"-"<<flush;
		}
	}
	position_super_kmers[number_kmer]=true;
	position_super_kmers.optimize();
	position_super_kmers.optimize_gap_size();
	position_super_kmers_RS=new bm::bvector<>::rs_index_type();
	position_super_kmers.build_rs_index(position_super_kmers_RS);
	cout<<endl;
	cout<<"----------------------INDEX RECAP----------------------------"<<endl;
	cout<<"Kmer in graph: "<<intToString(number_kmer)<<endl;
	cout<<"Super Kmer in graph: "<<intToString(number_super_kmer)<<endl;
	cout<<"Average size of Super Kmer: "<<intToString(number_kmer/(number_super_kmer))<<endl;
	cout<<"Total size of the partitionned graph: "<<intToString(bucketSeq.capacity()/2)<<endl;
	cout<<"Largest MPHF: "<<intToString(largest_MPHF)<<endl;
	cout<<"Largest Bucket: "<<intToString(largest_bucket_nuc_all)<<endl;
	cout<<"Size of the partitionned graph (MBytes): "<<intToString(bucketSeq.size()/(8*1024*1024))<<endl;
	cout<<"Total Positions size (MBytes): "<<intToString(positions.size()/(8*1024*1024))<<endl;
	cout<<"Size of the partitionned graph (bit per kmer): "<<((double)(bucketSeq.size())/(number_kmer))<<endl;
	bit_per_kmer+=((double)(bucketSeq.size())/(number_kmer));
	cout<<"Total Positions size (bit per kmer): "<<((double)positions.size()/number_kmer)<<endl;
	bit_per_kmer+=((double)positions.size()/number_kmer);
	cout<<"TOTAL Bits per kmer (without bbhash): "<<bit_per_kmer<<endl;
	cout<<"TOTAL Bits per kmer (with bbhash): "<<bit_per_kmer+4<<endl;
	cout<<"TOTAL Size estimated (MBytes): "<<(bit_per_kmer+4)*number_kmer/(8*1024*1024)<<endl;
}



inline kmer kmer_Set_Light::get_kmer(uint64_t mini,uint64_t pos){
	kmer res(0);
	uint64_t bit = (start_bucket[mini]+pos)*2;
	const uint64_t bitlast = bit + 2*k;
	for(;bit<bitlast;bit+=2){
		res<<=2;
		res |= bucketSeq[bit]*2 | bucketSeq[bit+1];
	}
	return res;
}



inline kmer kmer_Set_Light::get_kmer(uint64_t pos){
	kmer res(0);
	uint64_t bit = (pos)*2;
	const uint64_t bitlast = bit + 2*k;
	for(;bit<bitlast;bit+=2){
		res<<=2;
		res |= bucketSeq[bit]*2 | bucketSeq[bit+1];
	}
	return res;
}



inline kmer kmer_Set_Light::update_kmer(uint64_t pos,kmer mini,kmer input){
	return update_kmer_local(start_bucket[mini]+pos, bucketSeq, input);
}



inline kmer kmer_Set_Light::update_kmer_local(uint64_t pos,const vector<bool>& V,kmer input){
	input<<=2;
	uint64_t bit0 = pos*2;
	input |= V[bit0]*2 | V[bit0+1];
	return input%offsetUpdateAnchor;
}



void kmer_Set_Light::print_kmer(kmer num,uint64_t n){
	Pow2<kmer> anc(2*(k-1));
	for(uint64_t i(0);i<k and i<n;++i){
		uint64_t nuc=num/anc;
		num=num%anc;
		if(nuc==2){
			cout<<"T";
		}
		if(nuc==3){
			cout<<"G";
		}
		if(nuc==1){
			cout<<"C";
		}
		if(nuc==0){
			cout<<"A";
		}
		if (nuc>=4){
			cout<<nuc<<endl;
			cout<<"WTF"<<endl;
		}
		anc>>=2;
	}
	cout<<endl;
}



inline string kmer_Set_Light::kmer2str(kmer num){
	string res;
	Pow2<kmer> anc(2*(k-1));
	for(uint64_t i(0);i<k;++i){
		uint64_t nuc=num/anc;
		num=num%anc;
		if(nuc==3){
			res+="G";
		}
		if(nuc==2){
			res+="T";
		}
		if(nuc==1){
			res+="C";
		}
		if(nuc==0){
			res+="A";
		}
		if (nuc>=4){
			cout<<nuc<<endl;
			cout<<"WTF"<<endl;
		}
		anc>>=2;
	}
	return res;
}



void kmer_Set_Light::create_mphf_mem(uint64_t begin_BC,uint64_t end_BC){
	#pragma omp parallel  num_threads(coreNumber)
		{
		vector<kmer> anchors;
		uint32_t largest_bucket_anchor(0);
		uint32_t largest_bucket_nuc(0);
		#pragma omp for schedule(dynamic, number_bucket_per_mphf.value())
		for(uint64_t BC=(begin_BC);BC<end_BC;++BC){
			//~ if(all_buckets[BC].nuc_minimizer!=0){
			if(nuc_minimizer[BC]!=0){
				largest_bucket_nuc=max(largest_bucket_nuc,nuc_minimizer[BC]);
				largest_bucket_nuc_all=max(largest_bucket_nuc_all,nuc_minimizer[BC]);
				uint32_t bucketSize(1);
				kmer seq(get_kmer(BC,0)),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq));
				anchors.push_back(canon);
				for(uint64_t j(0);(j+k)<nuc_minimizer[BC];j++){
					if(position_super_kmers[all_mphf[BC].mphf_size+bucketSize]){
						j+=k-1;
						if((j+k)<nuc_minimizer[BC]){
							seq=(get_kmer(BC,j+1)),rcSeq=(rcb(seq)),canon=(min_k(seq,rcSeq));
							anchors.push_back(canon);
							bucketSize++;
						}
					}else{
						seq=update_kmer(j+k,BC,seq);
						rcSeq=(rcb(seq));
						canon=(min_k(seq, rcSeq));
						anchors.push_back(canon);
						bucketSize++;
					}
				}
				largest_bucket_anchor=max(largest_bucket_anchor,bucketSize);
			}
			if((BC+1)%number_bucket_per_mphf==0 and not anchors.empty()){
				largest_MPHF=max(largest_MPHF,anchors.size());
				all_mphf[BC/number_bucket_per_mphf].kmer_MPHF= new boomphf::mphf<kmer,hasher_t>(anchors.size(),anchors,gammaFactor);
				anchors.clear();
				largest_bucket_anchor=0;
				largest_bucket_nuc=(0);
			}
		}
	}
}



void kmer_Set_Light::create_mphf_disk(uint64_t begin_BC,uint64_t end_BC){
	#pragma omp parallel  num_threads(coreNumber)
		{
		uint32_t largest_bucket_anchor(0);
		uint32_t largest_bucket_nuc(0);
		#pragma omp for schedule(dynamic, number_bucket_per_mphf.value())
		for(uint64_t BC=(begin_BC);BC<end_BC;++BC){
			uint64_t mphfSize(0);
			string name(working_dir+"_blkmers"+to_string(BC));
			if(nuc_minimizer[BC]!=0){
				ofstream out(name,ofstream::binary | ofstream::trunc);
				largest_bucket_nuc=max(largest_bucket_nuc,nuc_minimizer[BC]);
				largest_bucket_nuc_all=max(largest_bucket_nuc_all,nuc_minimizer[BC]);
				uint32_t bucketSize(1);
				kmer seq(get_kmer(BC,0)),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq));
				out.write(reinterpret_cast<char*>(&canon),sizeof(canon));
				mphfSize++;
				for(uint64_t j(0);(j+k)<nuc_minimizer[BC];j++){
					if(position_super_kmers[all_mphf[BC].mphf_size+bucketSize]){
						j+=k-1;
						if((j+k)<nuc_minimizer[BC]){
							seq=(get_kmer(BC,j+1)),rcSeq=(rcb(seq)),canon=(min_k(seq,rcSeq));
							out.write(reinterpret_cast<char*>(&canon),sizeof(canon));
							bucketSize++;
							mphfSize++;
						}
					}else{
						seq=update_kmer(j+k,BC,seq);
						rcSeq=(rcb(seq));
						canon=(min_k(seq, rcSeq));
						out.write(reinterpret_cast<char*>(&canon),sizeof(canon));
						bucketSize++;
						mphfSize++;
					}
				}
				largest_bucket_anchor=max(largest_bucket_anchor,bucketSize);
			}
			if((BC+1)%number_bucket_per_mphf==0 and mphfSize!=0){
				largest_MPHF=max(largest_MPHF,mphfSize);
				auto data_iterator = file_binary(name.c_str());
				all_mphf[BC/number_bucket_per_mphf].kmer_MPHF= new boomphf::mphf<kmer,hasher_t>(mphfSize,data_iterator,gammaFactor);
				remove(name.c_str());
				largest_bucket_anchor=0;
				largest_bucket_nuc=(0);
				mphfSize=0;
			}
		}
	}
}



void kmer_Set_Light::int_to_bool(uint64_t n_bits_to_encode,uint64_t X, uint64_t pos,uint64_t start){
	for(uint64_t i(0);i<n_bits_to_encode;++i){
		uint64_t pos_check(i+pos*n_bits_to_encode+start);
		positions_mutex[(pos_check/64)*4096/(positions_int)].lock();
		positions[pos_check]=X%2;
		positions_mutex[(pos_check/64)*4096/(positions_int)].unlock();
		X>>=1;
	}
}



uint64_t kmer_Set_Light::bool_to_int(uint64_t n_bits_to_encode,uint64_t pos,uint64_t start){
	uint64_t res(0);
	uint64_t acc(1);
	for(uint64_t i(0);i<n_bits_to_encode;++i, acc<<=1){
		if(positions[i+pos*n_bits_to_encode+start]){
			res |= acc;
		}
	}
	return res;
}



void kmer_Set_Light::fill_positions(uint64_t begin_BC,uint64_t end_BC){
	#pragma omp parallel for num_threads(coreNumber)
	for(uint64_t BC=(begin_BC);BC<end_BC;++BC){
		uint64_t super_kmer_id(0);
		if(nuc_minimizer[BC]>0){
			uint64_t kmer_id(1);
			int n_bits_to_encode(all_mphf[BC/number_bucket_per_mphf].bit_to_encode);
			kmer seq(get_kmer(BC,0)),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq));
			int_to_bool(n_bits_to_encode,super_kmer_id/positions_to_check.value(),all_mphf[BC/number_bucket_per_mphf].kmer_MPHF->lookup(canon),all_mphf[BC/number_bucket_per_mphf].start);
			for(uint64_t j(0);(j+k)<nuc_minimizer[BC];j++){
				if(position_super_kmers[all_mphf[BC].mphf_size+kmer_id]){
					j+=k-1;
					super_kmer_id++;
					kmer_id++;
					if((j+k)<nuc_minimizer[BC]){
						seq=(get_kmer(BC,j+1)),rcSeq=(rcb(seq)),canon=(min_k(seq,rcSeq));
						{
							int_to_bool(n_bits_to_encode,super_kmer_id/positions_to_check.value(),all_mphf[BC/number_bucket_per_mphf].kmer_MPHF->lookup(canon),all_mphf[BC/number_bucket_per_mphf].start);
						}
					}
				}else{
					seq=update_kmer(j+k,BC,seq);
					rcSeq=(rcb(seq));
					canon=(min_k(seq, rcSeq));
					kmer_id++;
					{
						int_to_bool(n_bits_to_encode,super_kmer_id/positions_to_check.value(),all_mphf[BC/number_bucket_per_mphf].kmer_MPHF->lookup(canon),all_mphf[BC/number_bucket_per_mphf].start);
					}
				}
			}
		}
	}
}



int64_t kmer_Set_Light::kmer_to_hash(const kmer canon,kmer minimizer){
	if(unlikely(all_mphf[minimizer/number_bucket_per_mphf].empty)){return -1;}
	uint64_t hash=(all_mphf[minimizer/number_bucket_per_mphf].kmer_MPHF->lookup(canon));
	uint64_t pos;
	if(minimizer!=revhash(regular_minimizer_pos(canon,pos))%minimizer_number){
		cout<<minimizer<<" "<<revhash(regular_minimizer_pos(canon,pos))%minimizer_number<<endl;
			cout<<"NOP"<<endl;cin.get();
	}
	if(unlikely(hash == ULLONG_MAX)){
		return -1;
	}else{
		return hash;
	}
}



int64_t kmer_Set_Light::hash_to_rank(const int64_t hash,kmer minimizer){
	int n_bits_to_encode(all_mphf[minimizer/number_bucket_per_mphf].bit_to_encode);
	int64_t rank(all_buckets[minimizer].skmer_number+bool_to_int( n_bits_to_encode, hash, all_mphf[minimizer/number_bucket_per_mphf].start)*positions_to_check.value());
	return rank;
}



vector<kmer> kmer_Set_Light::kmer_to_superkmer(const kmer canon,kmer minimizer, int64_t& rank, int64_t& hash){
	hash=kmer_to_hash(canon,minimizer);
	if(hash<0){return {};}
	rank=(hash_to_rank(hash,minimizer));
	if(rank<0){return {};}
	vector<kmer> result;
	bool found(false);
	bm::id64_t pos,next_position,stop_position ;
	position_super_kmers.select(rank+1, pos, *(position_super_kmers_RS));
	for(uint64_t check_super_kmer(0);check_super_kmer<positions_to_check.value() and not found;++check_super_kmer){
		next_position=(position_super_kmers.get_next(pos));
		if(next_position==0){
			cout<<"no succesor"<<endl;
			stop_position=bucketSeq.size()-k;
		}else{
			stop_position=next_position+(rank+check_super_kmer)*(k-1);
		}
		pos+=(rank+check_super_kmer)*(k-1);

		if(likely(((uint64_t)pos+k-1)<bucketSeq.size())){
			kmer seqR=get_kmer(pos);
			kmer rcSeqR, canonR;
			for(uint64_t j=(pos);j<stop_position;++j){
				rcSeqR=(rcb(seqR));
				canonR=(min_k(seqR, rcSeqR));
				result.push_back(canonR);
				if(canon==canonR){
					found=true;
				}
				if(likely(((j+k)*2<bucketSeq.size()))){
					seqR=update_kmer_local(j+k, bucketSeq, seqR);
				}
			}
		}
		pos=next_position;
	}
	if(found){return result;}
	return{};
}



vector<bool> kmer_Set_Light::get_presence_query(const string& query){
	if(query.size()<k){return{};}
	vector<bool> result;
	kmer seq(str2num(query.substr(0,k))),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq));
	int64_t i(0),rank,hash;
	vector<kmer> superKmers;
	kmer minimizer(regular_minimizer(canon));
	for(;i+k<=query.size();++i){
		if(superKmers.empty()){
			superKmers=kmer_to_superkmer(canon,minimizer,rank,hash);
			result.push_back(not superKmers.empty());
		}else{
			if(kmer_in_superkmer(canon,superKmers)){
				result.push_back(true);
			}else{
				superKmers=kmer_to_superkmer(canon,minimizer,rank,hash);
				result.push_back(not superKmers.empty());
			}
		}
		if(i+k<query.size()){
			updateK(seq,query[i+k]);
			updateRCK(rcSeq,query[i+k]);
			canon=(min_k(seq, rcSeq));
			minimizer=(regular_minimizer(canon));
		}
	}
	return result;
}



vector<int64_t> kmer_Set_Light::get_rank_query(const string& query){
	if(query.size()<k){return{};}
	vector<int64_t> result;
	kmer seq(str2num(query.substr(0,k))),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq));
	int64_t i(0),rank,hash;
	vector<kmer> superKmers;
	kmer minimizer(regular_minimizer(canon));

	for(;i+k<=query.size();++i){
		if(superKmers.empty()){
			superKmers=kmer_to_superkmer(canon,minimizer,rank,hash);
			result.push_back(superKmers.empty()? -1 : rank);
		}else{
			if(kmer_in_superkmer(canon,superKmers)){
				result.push_back(rank);
			}else{
				superKmers=kmer_to_superkmer(canon,minimizer,rank,hash);
				result.push_back(superKmers.empty()? -1 : rank);
			}
		}
		if(i+k<query.size()){
			updateK(seq,query[i+k]);
			updateRCK(rcSeq,query[i+k]);
			canon=(min_k(seq, rcSeq));
			minimizer=regular_minimizer(canon);
		}
	}
	return result;
}



vector<int64_t> kmer_Set_Light::get_hashes_query(const string& query){
	if(query.size()<k){return{};}
	vector<int64_t> result;
	kmer seq(str2num(query.substr(0,k))),rcSeq(rcb(seq)),canon(min_k(seq,rcSeq));
	int64_t i(0),rank,hash;
	vector<kmer> superKmers;
	kmer minimizer(regular_minimizer(canon));

	uint64_t position;
	for(;i+k<=query.size();++i){
		if(superKmers.empty()){
			superKmers=kmer_to_superkmer(canon,minimizer,rank,hash);
			result.push_back(superKmers.empty() ? -1 : hash+all_mphf[minimizer/number_bucket_per_mphf].mphf_size);
		}else{
			if(kmer_in_superkmer(canon,superKmers)){
				result.push_back(kmer_to_hash(canon,minimizer)+all_mphf[minimizer/number_bucket_per_mphf].mphf_size);
			}else{
				superKmers=kmer_to_superkmer(canon,minimizer,rank,hash);
				result.push_back(superKmers.empty() ? -1 : hash+all_mphf[minimizer/number_bucket_per_mphf].mphf_size);
			}
		}
		if(i+k<query.size()){
			updateK(seq,query[i+k]);
			updateRCK(rcSeq,query[i+k]);
			canon=(min_k(seq, rcSeq));
			minimizer=(regular_minimizer(canon));
		}
	}
	return result;
}



void kmer_Set_Light::file_query_presence(const string& query_file){
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	auto in=new zstr::ifstream(query_file);
	atomic<uint64_t> TP(0),FP(0);
	#pragma omp parallel num_threads(coreNumber)
	{
		while(not in->eof() and in->good()){
			string query;
			vector<bool> presences;
			#pragma omp critical(dataupdate)
			{
				getline(*in,query);
				getline(*in,query);
			}
			if(query.size()>=k){
				presences=get_presence_query(query);
				uint64_t numberOne=count(presences.begin(), presences.end(), true);
				TP+=numberOne;
				FP+=presences.size()-numberOne;
				presences.clear();
			}
		}
	}
	cout<<endl<<"-----------------------QUERY PRESENCE RECAP ----------------------------"<<endl;
	cout<<"Good kmer: "<<intToString(TP)<<endl;
	cout<<"Erroneous kmers: "<<intToString(FP)<<endl;
	cout<<"Query performed: "<<intToString(FP+TP)<<endl;
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
	cout << "The whole QUERY took me " << time_span.count() << " seconds."<< endl;
	delete in;
}



void kmer_Set_Light::file_query_hases(const string& query_file, bool check){
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	auto in=new zstr::ifstream(query_file);
	vector<int64_t> all_hashes;
	#pragma omp parallel num_threads(coreNumber)
	{
		vector<kmer> kmerV;
		while(not in->eof() and in->good()){
			string query;
			vector<int64_t> query_hashes;
			#pragma omp critical(query_file)
			{
				getline(*in,query);
				getline(*in,query);
			}
			if(query.size()>=k){
				query_hashes=get_hashes_query(query);
				if(check){
					#pragma omp critical(query_file)
					{
						all_hashes.insert( all_hashes.end(), query_hashes.begin(), query_hashes.end() );
					}
				}
			}
		}
	}
	cout<<endl<<"-----------------------QUERY HASHES RECAP----------------------------"<<endl;
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
	cout << "The whole QUERY took me " << time_span.count() << " seconds."<< endl;
	if(not check){return;}
	sort(all_hashes.begin(),all_hashes.end());
	bool bijective(true);
	for(uint i(0);i<all_hashes.size();++i){
		if(all_hashes[i]!=i){
			cout<<all_hashes[i]<<":"<<i<<"	";
			bijective=false;
			// cin.get ();
			break;
		}
	}
	if(bijective){
		cout<<"HASHES ARE BIJECTIVE WITH [0::"<<all_hashes.size()-1<<"]"<<endl;
	}else{
		cout<<"HASHES ARE NOT BIJECTIVE !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"<<endl;
	}
	delete in;
}



void kmer_Set_Light::file_query_rank(const string& query_file){
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	auto in=new zstr::ifstream(query_file);
	vector<int64_t> all_hashes;
	#pragma omp parallel num_threads(coreNumber)
	{
		vector<kmer> kmerV;
		while(not in->eof() and in->good()){
			string query;
			vector<int64_t> query_hashes;
			#pragma omp critical(query_file)
			{
				getline(*in,query);
				getline(*in,query);
			}
			if(query.size()>=k){
				query_hashes=get_rank_query(query);
				#pragma omp critical(query_file)
				{
					all_hashes.insert( all_hashes.end(), query_hashes.begin(), query_hashes.end() );
				}
			}
		}
	}
	cout<<endl<<"-----------------------QUERY RANK RECAP----------------------------"<<endl;
	high_resolution_clock::time_point t2 = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
	cout << "The whole QUERY took me " << time_span.count() << " seconds."<< endl;
	sort(all_hashes.begin(),all_hashes.end());
	bool surjective(true);
	all_hashes.erase( unique( all_hashes.begin(), all_hashes.end() ), all_hashes.end() );

	for(uint i(0);i<all_hashes.size();++i){
		if(all_hashes[i]!=i){
			cout<<all_hashes[i]<<":"<<i<<"	";
			surjective=false;
			break;
		}
	}
	if(surjective){
		cout<<"RANKS ARE SURJECTIVE WITH [0::"<<all_hashes.size()-1<<"]"<<endl;
	}else{
		cout<<"RANKS ARE NOT SURJECTIVE !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"<<endl;
	}
	delete in;
}



void kmer_Set_Light::file_query_all_test(const string& query_file, bool full){
	file_query_presence(query_file);
	if(not full){return;}
	file_query_hases(query_file);
	file_query_rank(query_file);
}



void kmer_Set_Light::dump_disk(const string& output_file){
	//OPEN
	filebuf fb;
	remove(output_file.c_str());
	fb.open (output_file, ios::out | ios::binary | ios::trunc);
	zstr::ostream out(&fb);

	//VARIOUS INTEGERS
	out.write(reinterpret_cast<const char*>(&k),sizeof(k));
	out.write(reinterpret_cast<const char*>(&m1),sizeof(m1));
	out.write(reinterpret_cast<const char*>(&m3),sizeof(m3));
	out.write(reinterpret_cast<const char*>(&minimizer_size_graph),sizeof(minimizer_size_graph));
	out.write(reinterpret_cast<const char*>(&bit_saved_sub),sizeof(bit_saved_sub));
	uint64_t positions_size(positions.size()), bucketSeq_size(bucketSeq.size());
	out.write(reinterpret_cast<const char*>(&positions_size),sizeof(positions_size));
	out.write(reinterpret_cast<const char*>(&bucketSeq_size),sizeof(bucketSeq_size));

	//BOOL VECTOR
	dump_vector_bool(bucketSeq,&out);
	dump_vector_bool(positions,&out);

	//BUCKETS INFORMATION

	for(uint64_t i(0);i<mphf_number;++i){
	out.write(reinterpret_cast<const char*>(&all_mphf[i].mphf_size),sizeof(all_mphf[i].mphf_size));
	out.write(reinterpret_cast<const char*>(&all_mphf[i].empty),sizeof(all_mphf[i].empty));
	out.write(reinterpret_cast<const char*>(&all_mphf[i].start),sizeof(all_mphf[i].start));
	out.write(reinterpret_cast<const char*>(&all_mphf[i].bit_to_encode),sizeof(all_mphf[i].bit_to_encode));
	if(not all_mphf[i].empty){
			all_mphf[i].kmer_MPHF->save(out);
		}
	}
	for(uint64_t i(0);i<minimizer_number;++i){
		out.write(reinterpret_cast<const char*>(&all_buckets[i].skmer_number),sizeof(all_buckets[i].skmer_number));
	}

	//BM VECTOR
	bm::serializer<bm::bvector<> > bvs;
	bvs.byte_order_serialization(false);
	bvs.gap_length_serialization(false);
	bm::serializer<bm::bvector<> >::buffer sbuf;
	{
		unsigned char* buf = 0;
		bvs.serialize(position_super_kmers, sbuf);
		buf = sbuf.data();
		uint64_t sz = sbuf.size();
		auto point2 =&buf[0];
		out.write(reinterpret_cast<const char*>(&sz),sizeof(sz));
		out.write((char*)point2,sz);
	}

	out<<flush;
	fb.close();
}



void kmer_Set_Light::dump_and_destroy(const string& output_file){
	//OPEN
	filebuf fb;
	remove(output_file.c_str());
	fb.open (output_file, ios::out | ios::binary | ios::trunc);
	zstr::ostream out(&fb);

	//VARIOUS INTEGERS
	out.write(reinterpret_cast<const char*>(&k),sizeof(k));
	out.write(reinterpret_cast<const char*>(&m1),sizeof(m1));
	out.write(reinterpret_cast<const char*>(&m3),sizeof(m3));
	out.write(reinterpret_cast<const char*>(&minimizer_size_graph),sizeof(minimizer_size_graph));
	out.write(reinterpret_cast<const char*>(&bit_saved_sub),sizeof(bit_saved_sub));
	uint64_t positions_size(positions.size()), bucketSeq_size(bucketSeq.size());
	out.write(reinterpret_cast<const char*>(&positions_size),sizeof(positions_size));
	out.write(reinterpret_cast<const char*>(&bucketSeq_size),sizeof(bucketSeq_size));

	//BOOL VECTOR
	vector<bool> nadine;
	dump_vector_bool(bucketSeq,&out);
	bucketSeq.clear();
	bucketSeq.swap(nadine);
	dump_vector_bool(positions,&out);
	positions.clear();
	positions.swap(nadine);

	//BCUKETS INFORMATION
	for(uint64_t i(0);i<mphf_number;++i){
		out.write(reinterpret_cast<const char*>(&all_mphf[i].mphf_size),sizeof(all_mphf[i].mphf_size));
		out.write(reinterpret_cast<const char*>(&all_mphf[i].empty),sizeof(all_mphf[i].empty));
		out.write(reinterpret_cast<const char*>(&all_mphf[i].start),sizeof(all_mphf[i].start));
		out.write(reinterpret_cast<const char*>(&all_mphf[i].bit_to_encode),sizeof(all_mphf[i].bit_to_encode));
		if(not all_mphf[i].empty){
			all_mphf[i].kmer_MPHF->save(out);
			delete all_mphf[i].kmer_MPHF;
			all_mphf[i].empty=true;
		}
	}
	for(uint64_t i(0);i<minimizer_number;++i){
		out.write(reinterpret_cast<const char*>(&all_buckets[i].skmer_number),sizeof(all_buckets[i].skmer_number));
	}

	//BM VECTOR
	bm::serializer<bm::bvector<> > bvs;
	bvs.byte_order_serialization(false);
	bvs.gap_length_serialization(false);
	bm::serializer<bm::bvector<> >::buffer sbuf;
	{
		unsigned char* buf = 0;
		bvs.serialize(position_super_kmers, sbuf);
		buf = sbuf.data();
		uint64_t sz = sbuf.size();
		auto point2 =&buf[0];
		out.write(reinterpret_cast<const char*>(&sz),sizeof(sz));
		out.write((char*)point2,sz);
	}
	out<<flush;
	fb.close();
}



kmer_Set_Light::kmer_Set_Light(const string& index_file){
	zstr::ifstream out(index_file);
	//VARIOUS INTEGERS
	out.read(reinterpret_cast< char*>(&k),sizeof(k));
	out.read(reinterpret_cast< char*>(&m1),sizeof(m1));
	m2=m1;
	coreNumber=20;
	uint64_t  bucketSeq_size, positions_size;
	out.read(reinterpret_cast< char*>(&m3),sizeof(m3));
	out.read(reinterpret_cast< char*>(&minimizer_size_graph),sizeof(minimizer_size_graph));
	out.read(reinterpret_cast< char*>(&bit_saved_sub),sizeof(bit_saved_sub));
	out.read(reinterpret_cast< char*>(&positions_size),sizeof(positions_size));
	out.read(reinterpret_cast< char*>(&bucketSeq_size),sizeof(bucketSeq_size));
	offsetUpdateAnchor=(2*k);
	offsetUpdateMinimizer=(2*minimizer_size_graph);
	mphf_number=(2*m2);
	number_superbuckets=(2*m3);
	minimizer_number=(2*m1);
	minimizer_number_graph=(2*minimizer_size_graph);
	number_bucket_per_mphf=(2*(m1-m2));
	bucket_per_superBuckets=(2*(m1-m3));
	positions_to_check=(bit_saved_sub);
	gammaFactor=2;

	//BOOL VECTOR
	read_vector_bool(bucketSeq,&out,bucketSeq_size);
	read_vector_bool(positions,&out,positions_size);

	//BUCKETS INFORMATION
	all_buckets=new bucket_minimizer[minimizer_number.value()]();
	all_mphf=new info_mphf[mphf_number.value()];
	for(uint64_t i(0);i<mphf_number;++i){
		out.read(reinterpret_cast< char*>(&all_mphf[i].mphf_size),sizeof(all_mphf[i].mphf_size));
		out.read(reinterpret_cast< char*>(&all_mphf[i].empty),sizeof(all_mphf[i].empty));
		out.read(reinterpret_cast< char*>(&all_mphf[i].start),sizeof(all_mphf[i].start));
		out.read(reinterpret_cast< char*>(&all_mphf[i].bit_to_encode),sizeof(all_mphf[i].bit_to_encode));
		if(not all_mphf[i].empty){
			all_mphf[i].kmer_MPHF=new boomphf::mphf<kmer,hasher_t>;
			all_mphf[i].kmer_MPHF->load(out);
		}
	}
	for(uint64_t i(0);i<minimizer_number;++i){
		out.read(reinterpret_cast< char*>(&all_buckets[i].skmer_number),sizeof(all_buckets[i].skmer_number));
	}

	//BM VECTOR
	uint64_t sz;
	out.read(reinterpret_cast< char*>(&sz),sizeof(sz));
	uint8_t* buff= new uint8_t[sz];
	out.read((char*)buff,sz);
	bm::deserialize(position_super_kmers, buff);
	delete buff;

	position_super_kmers_RS=new bm::bvector<>::rs_index_type();
	position_super_kmers.build_rs_index(position_super_kmers_RS);

	cout<<"Index loaded"<<endl;
}
