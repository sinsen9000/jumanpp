#include "common.h"
#include "cmdline.h"
#include "feature.h"
#include "tagger.h"
#include "sentence.h"
#include <memory.h>

//namespace SRILM {
//#include "srilm/Ngram.h"
//#include "srilm/Vocab.h"
//}

bool MODE_TRAIN = false;
bool WEIGHT_AVERAGED = false;

// オプション
void option_proc(cmdline::parser &option, int argc, char **argv) {//{{{
    option.add<std::string>("dict", 'd', "dict filename", false, "data/japanese.dic");
    option.add<std::string>("model", 'm', "model filename", false, "data/model.mdl");
    option.add<std::string>("rnnlm", 'r', "rnnlm filename", false, "data/lang.mdl"); 
    option.add<std::string>("feature", 'f', "feature template filename", false, "data/feature.def");

#ifdef USE_SRILM
    option.add<std::string>("srilm", 'I', "srilm filename", false, "srilm.arpa");
#endif
        
    option.add<std::string>("train", 't', "training filename", false, "data/train.txt");
    option.add<std::string>("ptrain", 'p', "partial training filename", false, "data/ptrain.txt");
    option.add<unsigned int>("iteration", 'i', "iteration number for training", false, 10);
    option.add("ptest", 0, "receive partially annotated text");
        
    option.add<unsigned int>("unk_max_length", 'l', "maximum length of unknown word detection", false, 2);
    option.add<unsigned int>("lattice", 'L', "output lattice format",false, 1);
    option.add<double>("Cvalue", 'C', "C value",false, 1.0);
    option.add<double>("Phi", 'P', "Phi value",false, 1.65);
    option.add<double>("Rweight", 0, "linear interpolation parameter for KKN score and RNN score",false, 0.3);
    option.add<double>("Lweight", 0, "linear penalty parameter for unknown words in RNNLM",false, 1.5);
    option.add<unsigned int>("nbest", 'n', "n-best search", false, 5);
    option.add<unsigned int>("rerank", 'R', "n-best reranking", false, 5);
    option.add("scw", 0, "use soft confidence weighted");
    option.add("oldloss", 0, "use old loss function");
    option.add<std::string>("lda", 0, "use lda", false, ""); //廃止予定
    option.add<unsigned int>("beam", 'b', "use beam search",false,5);
    option.add<unsigned int>("rbeam", 'B', "use beam search and reranking",false,5);
    option.add("oldstyle", 'o', "print old style lattice");
    option.add("juman", 'j', "print juman style");
    option.add("averaged", 'a', "use averaged perceptron for training");
    option.add("total", 'T', "use total similarity for LDA");
    option.add("ambiguous", 'A', "output ambiguous words on lattice");
    option.add("shuffle", 's', "shuffle training data for each iteration");
    option.add("passiveunk", '\0', "apply passive unknown word detection. The option use unknown word detection only if it cannot make any node."); 
    option.add("notrigram", 0, "do NOT use trigram feature");
    option.add("rnnasfeature", 0, "use rnnlm score as feature");
    option.add("unknown", 'u', "apply unknown word detection (obsolete; already default)");
    option.add<std::string>("use_lexical_feature", '\0', "change frequent word list for lexical feature",false,"data/freq_words.list"); 
    option.add("part", '\0', "use partical annotation");
    option.add("debug", '\0', "debug mode");
    option.add("rnndebug", '\0', "show rnnlm debug message");
    option.add("version", 'v', "print version");
    option.add("help", 'h', "print this message");
    option.parse_check(argc, argv);
    if (option.exist("version")) {//{{{
        cout << "KKN " << VERSION << "(" << GITVER <<")" << endl;
        exit(0);
    }//}}}
    if (option.exist("train")) {//{{{
        MODE_TRAIN = true;
    }//}}}
    if (option.exist("averaged")) {//{{{
        WEIGHT_AVERAGED = true;
    }//}}}
}//}}}

// unit_test 時はmainを除く
#ifndef KKN_UNIT_TEST

//終了時にdartsのclearでbus errorが出てる

int main(int argc, char** argv) {//{{{
    cmdline::parser option;
    option_proc(option, argc, argv);

    Morph::Parameter param(option.get<std::string>("dict"), option.get<std::string>("feature"), option.get<unsigned int>("iteration"), true, option.exist("shuffle"), option.get<unsigned int>("unk_max_length"), option.exist("debug"), option.exist("nbest")|option.exist("lattice"));

    // 基本的にbeam は使う
    param.set_beam(true);
    param.set_N(5); //初期値

    // beam, rbeam 以外のオプションを廃止予定
    if(option.exist("nbest"))
        param.set_N(option.get<unsigned int>("nbest"));
    else if(option.exist("beam")){
        param.set_beam(true);
        param.set_N(option.get<unsigned int>("beam"));
    }else if(option.exist("rbeam")){
        param.set_N(option.get<unsigned int>("rbeam"));
    }else{
        param.set_N(1);
    }

    if(option.exist("lattice")){ 
        // rbeam が設定されていたら，lattice のN は表示のみに使う
        if(option.exist("rbeam")){
            param.L = option.get<unsigned int>("lattice");
        }else{// 無ければラティスと同じ幅を指定
            param.set_N(option.get<unsigned int>("lattice"));
            param.L = option.get<unsigned int>("lattice");
        }
    }    
    
    param.set_output_ambigous_word(option.exist("ambiguous"));
    if(option.exist("passiveunk"))
        param.set_passive_unknown(true);
    param.set_model_filename(option.get<std::string>("model"));
    param.set_use_scw(option.exist("scw"));
    param.set_lweight(option.get<double>("Lweight"));
    if(option.exist("Cvalue"))
        param.set_C(option.get<double>("Cvalue"));
    if(option.exist("Phi"))
        param.set_Phi(option.get<double>("Phi"));
    if(option.exist("total")){ // LDA用, 廃止予定
        param.set_use_total_sim();
        Morph::FeatureSet::use_total_sim = true;
    }
    // 部分アノテーション用デリミタ
    param.delimiter = "\t";

    if(option.exist("rnnasfeature") && option.exist("rnnlm")){
        param.use_rnnlm_as_feature = true;
    }

    param.use_lexical_feature=true;
    param.freq_word_list = option.get<std::string>("use_lexical_feature");
    Morph::FeatureSet::open_freq_word_set(param.freq_word_list);
    if(option.exist("debug")){
        Morph::FeatureSet::debug_flag = true;
    }

    // trigram は基本的に使う
    param.set_trigram(!option.exist("notrigram"));
    param.set_rweight(option.get<double>("Rweight"));

    if(option.exist("oldloss")){ //廃止予定
        param.useoldloss = true;
    }
    param.use_suu_rule = true;
    param.no_posmatch = true;

    Morph::Parameter normal_param = param;
    normal_param.set_nbest(true); // nbest を利用するよう設定
    normal_param.set_N(10); // 10-best に設定
    param.set_rnnlm(option.exist("rnnlm"));
    param.set_nce(option.exist("rnnlm"));

#ifdef USE_SRILM
    param.set_srilm(option.exist("srilm"));
#endif
    Morph::Tagger tagger(&param);
    Morph::Node::set_param(&param);
   
    RNNLM::CRnnLM rnnlm;
    if(option.exist("rnnlm")){
        rnnlm.setLambda(1.0);
        rnnlm.setRegularization(0.0000001);
        rnnlm.setDynamic(0);
        rnnlm.setRnnLMFile(option.get<std::string>("rnnlm").c_str());
        rnnlm.setRandSeed(1);
        rnnlm.useLMProb(0);
        if(option.exist("rnndebug"))
            rnnlm.setDebugMode(1);
        else
            rnnlm.setDebugMode(0);
        if(param.lpenalty)
            rnnlm.setLweight(param.lweight);
        srand(1);
        Morph::Sentence::init_rnnlm_FR(&rnnlm);
    } 
#ifdef USE_SRILM /*{{{*/
    Vocab *vocab;
    Ngram *ngramLM;
    if(option.exist("srilm")){//
        vocab = new Vocab;
        vocab->unkIsWord() = true; // use unknown <unk> 
        //File file(vocabFile, "r"); // vocabFile
        //vocab->read(file); //オプションでは指定してない
        ngramLM = new Ngram(*vocab, 3); //order = 3,or 4?

        std::string lmfile = option.get<std::string>("srilm"); // "./srilm.vocab";
        //SRILM::
        File file(lmfile.c_str(), "r");//
        bool limitVocab = false; //?

	    if (!ngramLM->read(file, limitVocab)) {
            cerr << "format error in lm file " << lmfile << endl;
            exit(1);
	    }
        //ngramLM->skipOOVs() = true; 
        //ngramLM->linearPenalty() = true; // 未知語のスコアの付け方を変更

        Morph::Sentence::init_srilm(ngramLM, vocab);
    }
#endif /*}}}*/

    if (option.exist("ptrain")) {//部分アノテーション学習モード{{{
        std::cerr << "done" << std::endl;
        tagger.ptrain(option.get<std::string>("train"));
        tagger.write_bin_model_file(option.get<std::string>("model"));
      //}}}
    } else if (option.exist("ptest")) {//部分アノテーション付き形態素解析{{{
        tagger.read_bin_model_file(option.get<std::string>("model"));
        std::cerr << "done" << std::endl;
        param.delimiter = "\t";
            
        std::string buffer;
        while (getline(cin, buffer)) {
            
            std::cerr << "input:" << buffer << std::endl;
            Morph::Sentence* pa_sent = tagger.partial_annotation_analyze(buffer);
        
            //if(option.exist("juman"))
                tagger.print_best_beam_juman();
            //else
            //    tagger.print_best_beam();
            tagger.sentence_clear();
        }
      //}}}
    } else if (MODE_TRAIN) {//学習モード{{{
        std::cerr << "done" << std::endl;
        if(option.exist("lda")){
            std::cerr << "LDA training is obsoleted" << std::endl;
            return 0;
            Morph::Tagger tagger_normal(&normal_param);
            tagger_normal.read_model_file(option.get<std::string>("lda"));
                
            tagger.train_lda(option.get<std::string>("train"), tagger_normal);
            tagger.write_bin_model_file(option.get<std::string>("model"));
        }else if (option.exist("part")) { //部分的アノテーションからの学習
            tagger.read_bin_model_file(option.get<std::string>("model"));
            // diagの読み込み
            //tagger.ptrain(option.get<std::string>("train"));
            tagger.write_bin_model_file(option.get<std::string>("model")+"+"); //ココのファイル名はオプションで与えられるようにする
        }else{ //通常の学習
            tagger.train(option.get<std::string>("train"));
            tagger.write_bin_model_file(option.get<std::string>("model"));
        }
      //}}}
    } else if(option.exist("lda")){//LDAを使う形態素解析{{{
        std::cerr << "LDA mode is depreciated" << std::endl;
        return 0;
//        Morph::Tagger tagger_normal(&normal_param);
//        tagger_normal.read_model_file(option.get<std::string>("lda"));
//        tagger.read_model_file(option.get<std::string>("model"));
//        std::cerr << "done" << std::endl;
//            
//        std::ifstream is(argv[1]); // input stream
//            
//        // sentence loop
//        std::string buffer;
//        while (getline(is ? is : cin, buffer)) {
//            if (buffer.length() == 0 || buffer.at(0) == '#') { // empty line or comment line
//                cout << buffer << endl;
//                continue;
//            }
//            Morph::Sentence* normal_sent = tagger_normal.new_sentence_analyze(buffer);
//            TopicVector topic = normal_sent->get_topic();
//
//            //std::cerr << "Topic:";
//            //for(double d: topic){
//            //    std::cerr << d << ",";
//            //}
//            //std::cerr << endl;
//
//            Morph::Sentence* lda_sent = tagger.new_sentence_analyze_lda(buffer, topic);
//            if (option.exist("lattice")){
//                if (option.exist("oldstyle"))
//                    tagger.print_old_lattice();
//                else
//                    tagger.print_lattice();
//            }else{
//                if(option.exist("nbest")){
//                    tagger.print_N_best_path();
//                }else{
//                    tagger.print_best_path();
//                }
//            }
//            delete(normal_sent);
//            delete(lda_sent);
//        }
    //}}}
    } else {// 通常の形態素解析{{{
        tagger.read_bin_model_file(option.get<std::string>("model"));
        if(param.debug) std::cerr << "done" << std::endl;
            
        std::ifstream is(argv[1]); // input stream
            
        // sentence loop
        std::string buffer;
        while (getline(is ? is : cin, buffer)) {
            if (buffer.length() <= 1 ) { // empty line or comment line
                std::cout << buffer << std::endl;
                continue;
            }else if(buffer.at(0) == '#'){
                // 動的コマンドの処理
                std::size_t pos;
                if( (pos = buffer.find("##KKN\t")) != std::string::npos ){
                    std::size_t arg_pos;
                    // input:
                    // ##KKN<tab>command arg
                    // ##KKN<tab>setL 5
                        
                    // setL command
                    std::string command = "setL";
                    if( (arg_pos = buffer.find("setL")) != std::string::npos){
                        arg_pos = buffer.find_first_of(" \t",arg_pos+command.length());
                        long val = std::stol(buffer.substr(arg_pos));
                        param.L = val;
                        std::cout << "##KKN\tsetL " << val << std::endl;
                    }

                    // setN command
                    command = "setN";
                    if( (arg_pos = buffer.find("setN")) != std::string::npos){
                        arg_pos = buffer.find_first_of(" \t",arg_pos+command.length());
                        long val = std::stol(buffer.substr(arg_pos));
                        param.N = val;
                        std::cout << "##KKN\tsetN " << val << std::endl;
                    }

                }else{// S-ID の処理
                    std::cout << buffer << " " << VERSION << "(" << GITVER <<")" << std::endl;
                }
                continue;
            }

            tagger.new_sentence_analyze(buffer);
            if (option.exist("lattice")){
                if(option.exist("rbeam")){
                    tagger.print_lattice_rbeam(param.L);
                }else{
                    if (option.exist("oldstyle"))
                        tagger.print_old_lattice();
                    else
                        tagger.print_lattice();
                }
            }else{
                if(option.exist("beam")){
                    tagger.print_beam();
                }else if(option.exist("rbeam")){
                    if(option.exist("juman"))
                        tagger.print_best_beam_juman();
                    else
                        tagger.print_best_beam();
                }else{
                    tagger.print_best_beam_juman();
                }
            }
            tagger.sentence_clear();
        }
    }//}}}
    return 0;
}//}}}

#endif
