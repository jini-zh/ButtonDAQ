#include "CalibTrigger.h"

CalibTrigger_args::CalibTrigger_args():Thread_args(){}

CalibTrigger_args::~CalibTrigger_args(){}


CalibTrigger::CalibTrigger():Tool(){}


bool CalibTrigger::Initialise(std::string configfile, DataModel &data){
  InitialiseTool(data);
  InitialiseConfiguration(configfile);

  if(!m_variables.Get("verbose",m_verbose)) m_verbose=1;

  m_util=new Utilities();

  m_threadnum=0;
  CreateThread();
  
  m_freethreads=1;

  ExportConfiguration();
  return true;
}


bool CalibTrigger::Execute(){

  for(unsigned int i=0; i<args.size(); i++){
    if(args.at(i)->busy==0){
      std::cout<<"reply="<<args.at(i)->message<<std::endl;
      args.at(i)->message="Hi";
      args.at(i)->busy=1;
      break;
    }

  }

  m_freethreads=0;
  int lastfree=0;
  for(unsigned int i=0; i<args.size(); i++){
    if(args.at(i)->busy==0){
      m_freethreads++;
      lastfree=i; 
    }
  }

  if(m_freethreads<1) CreateThread();
  if(m_freethreads>1) DeleteThread(lastfree);
  
  std::cout<<"free threads="<<m_freethreads<<":"<<args.size()<<std::endl;
  
  sleep(1);
  
  return true;
}


bool CalibTrigger::Finalise(){

  for(unsigned int i=0;i<args.size();i++) m_util->KillThread(args.at(i));
  
  args.clear();
  
  delete m_util;
  m_util=0;

  return true;
}

void CalibTrigger::CreateThread(){

  CalibTrigger_args* tmparg=new CalibTrigger_args();
  tmparg->busy=0;
  tmparg->message="";
  args.push_back(tmparg);
  std::stringstream tmp;
  tmp<<"T"<<m_threadnum;
  m_util->CreateThread(tmp.str(), &Thread, args.at(args.size()-1));
  m_threadnum++;

}

 void CalibTrigger::DeleteThread(int pos){

   m_util->KillThread(args.at(pos));
   delete args.at(pos);
   args.at(pos)=0;
   args.erase(args.begin()+(pos-1));

 }

void CalibTrigger::Thread(Thread_args* arg){

  CalibTrigger_args* args=reinterpret_cast<CalibTrigger_args*>(arg);

  if(!args->busy) usleep(100);
  else{ 

    args->message="Hello";
    sleep(10);

    args->busy=0;
  }

}
