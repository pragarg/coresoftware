// $Id: Prototype2DSTReader.h,v 1.7 2015/02/27 23:42:23 jinhuang Exp $

/*!
 * \file Prototype2DSTReader.h
 * \brief 
 * \author Jin Huang <jhuang@bnl.gov>
 * \version $Revision: 1.7 $
 * \date $Date: 2015/02/27 23:42:23 $
 */

#ifndef Prototype2DSTReader_H_
#define Prototype2DSTReader_H_

#include <HepMC/GenEvent.h>
#include <HepMC/SimpleVector.h>
#include <fun4all/SubsysReco.h>
#include <string>
#include <iostream>
#include <vector>
#include <TClonesArray.h>
#include "RawTower_Prototype2.h"
#include "RawTower_Temperature.h"

class TTree;

#ifndef __CINT__

#include <boost/smart_ptr.hpp>

#endif

/*!
 * \brief Prototype2DSTReader save information from DST to an evaluator, which could include hit. particle, vertex, towers and jet (to be activated)
 */
class Prototype2DSTReader : public SubsysReco
{
public:
  Prototype2DSTReader(const std::string &filename);
  virtual
  ~Prototype2DSTReader();

  //! full initialization
  int
  Init(PHCompositeNode *);

  //! event processing method
  int
  process_event(PHCompositeNode *);

  //! end of run method
  int
  End(PHCompositeNode *);

  void
  AddTower(const std::string &name)
  {
    _tower_postfix.push_back(name);
  }

  void
  AddTowerTemperature(const std::string &name)
  {
    _towertemp_postfix.push_back(name);
  }

  void
  AddRunInfo(const std::string &name)
  {
    _runinfo_list.push_back(name);
  }

  //! zero suppression for all calorimeters
  double
  get_tower_zero_sup()
  {
    return _tower_zero_sup;
  }

  //! zero suppression for all calorimeters
  void
  set_tower_zero_sup(double b)
  {
    _tower_zero_sup = b;
  }

protected:

//  std::vector<std::string> _node_postfix;
  std::vector<std::string> _tower_postfix;
  //! tower temperature
  std::vector<std::string> _towertemp_postfix;
//  std::vector<std::string> _jet_postfix;
//  std::vector<std::string> _node_name;
  std::vector<std::string> _runinfo_list;

  int nblocks;

#ifndef __CINT__

  typedef boost::shared_ptr<TClonesArray> arr_ptr;

  struct record
  {
    unsigned int _cnt;
    std::string _name;
    arr_ptr _arr;
    TClonesArray * _arr_ptr;
    double _dvalue;

    enum enu_type
    {
      typ_hit, typ_part, typ_vertex, typ_tower, typ_jets, typ_runinfo, typ_towertemp
    };
    enu_type _type;
  };
  typedef std::vector<record> records_t;
  records_t _records;

  typedef RawTower_Prototype2 RawTower_type;

  typedef RawTower_Temperature RawTowerT_type;
#endif

  int _event;

  std::string _out_file_name;

//  TFile * _file;
  TTree * _T;

  //! zero suppression for all calorimeters
  double _tower_zero_sup;

  void
  build_tree();
};

#endif /* Prototype2DSTReader_H_ */
