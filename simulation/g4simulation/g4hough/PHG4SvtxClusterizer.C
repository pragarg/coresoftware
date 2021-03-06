#include "PHG4SvtxClusterizer.h"

#include "SvtxHitMap.h"
#include "SvtxHit.h"
#include "SvtxClusterMap.h"
#include "SvtxClusterMap_v1.h"
#include "SvtxCluster.h"
#include "SvtxCluster_v1.h"

#include <g4main/PHG4Hit.h>
#include <g4main/PHG4HitContainer.h>
#include <fun4all/Fun4AllReturnCodes.h>
#include <phool/PHCompositeNode.h>
#include <phool/PHIODataNode.h>
#include <phool/PHNodeIterator.h>
#include <phool/getClass.h>
#include <g4detectors/PHG4CylinderCellContainer.h>
#include <g4detectors/PHG4CylinderCellGeomContainer.h>
#include <g4detectors/PHG4CylinderGeomContainer.h>
#include <g4detectors/PHG4CylinderGeom.h>
#include <g4detectors/PHG4CylinderCell.h>
#include <g4detectors/PHG4CylinderCellGeom.h>

#include <boost/tuple/tuple.hpp>
#include <boost/format.hpp>

#include <TMatrixF.h>

#define BOOST_NO_HASH // Our version of boost.graph is incompatible with GCC-4.3 w/o this flag
#include <boost/bind.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>
using namespace boost;

#include <iostream>
#include <stdexcept>
#include <cmath>

using namespace std;

static const float twopi = 2.0*M_PI;

bool PHG4SvtxClusterizer::lessthan(const PHG4CylinderCell* lhs, 
				   const PHG4CylinderCell* rhs) {

  if( lhs->get_binphi() < rhs->get_binphi() ) return true;
  else if( lhs->get_binphi() == rhs->get_binphi() ){
    if( lhs->get_binz() < rhs->get_binz() ) return true;
  }

  return false;
}

bool PHG4SvtxClusterizer::ladder_lessthan(const PHG4CylinderCell* lhs, 
					  const PHG4CylinderCell* rhs) {

  if ( lhs->get_sensor_index() == rhs->get_sensor_index() ) { 

    if( lhs->get_binphi() < rhs->get_binphi() ) return true;
    else if( lhs->get_binphi() == rhs->get_binphi() ){
      if( lhs->get_binz() < rhs->get_binz() ) return true;
    }
    
  } else {
    if ( lhs->get_sensor_index() < rhs->get_sensor_index() ) return true;   
  }
    
  return false;
}

bool PHG4SvtxClusterizer::are_adjacent(const PHG4CylinderCell* lhs, 
				       const PHG4CylinderCell* rhs,
                                       const int &nphibins) {

  int lhs_layer = lhs->get_layer();
  int rhs_layer = rhs->get_layer();
  if (lhs_layer != rhs_layer) return false;

  if (get_z_clustering(lhs_layer)) {
    if( fabs(lhs->get_binz() - rhs->get_binz()) <= 1 ) {
      if( fabs(lhs->get_binphi() - rhs->get_binphi()) <= 1 ){
	return true;
      } else if(lhs->get_binphi() == 0 || rhs->get_binphi() == 0) {
	if(fabs(lhs->get_binphi() - rhs->get_binphi()) == (nphibins-1))
	  return true;
      }
    }
  } else {
    if( fabs(lhs->get_binz() - rhs->get_binz()) == 0 ) {
      if( fabs(lhs->get_binphi() - rhs->get_binphi()) <= 1 ){
	return true;
      } else if(lhs->get_binphi() == 0 || rhs->get_binphi() == 0) {
	if(fabs(lhs->get_binphi() - rhs->get_binphi()) == (nphibins-1))
	  return true;
      }
    }
  }

  return false;
}

bool PHG4SvtxClusterizer::ladder_are_adjacent(const PHG4CylinderCell* lhs, 
					      const PHG4CylinderCell* rhs) {
  int lhs_layer = lhs->get_layer();
  int rhs_layer = rhs->get_layer();
  if (lhs_layer != rhs_layer) return false;

  if (lhs->get_sensor_index() != rhs->get_sensor_index()) return false;
  
  if (get_z_clustering(lhs_layer)) {
    if( fabs(lhs->get_binz() - rhs->get_binz()) <= 1 ) {
      if( fabs(lhs->get_binphi() - rhs->get_binphi()) <= 1 ){
	return true;
      }
    }
  } else {
    if( fabs(lhs->get_binz() - rhs->get_binz()) == 0 ) {
      if( fabs(lhs->get_binphi() - rhs->get_binphi()) <= 1 ){
	return true;
      } 
    }
  }

  return false;
}

PHG4SvtxClusterizer::PHG4SvtxClusterizer(const string &name,
					 unsigned int min_layer,
					 unsigned int max_layer) :
  SubsysReco(name),
  _hits(NULL),
  _clusterlist(NULL),
  _fraction_of_mip(0.5),
  _thresholds_by_layer(),
  _make_z_clustering(),
  _make_e_weights(),
  _min_layer(min_layer),
  _max_layer(max_layer),
  _timer(PHTimeServer::get()->insert_new(name)) {}

int PHG4SvtxClusterizer::InitRun(PHCompositeNode* topNode) {

  // get node containing the digitized hits
  _hits = findNode::getClass<SvtxHitMap>(topNode,"SvtxHitMap");
  if (!_hits) {
    cout << PHWHERE << "ERROR: Can't find node SvtxHitMap" << endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }
  
  //-----------------
  // Add Cluster Node
  //-----------------

  PHNodeIterator iter(topNode);

  // Looking for the DST node
  PHCompositeNode *dstNode 
    = dynamic_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode","DST"));
  if (!dstNode) {
    cout << PHWHERE << "DST Node missing, doing nothing." << endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }
    
  // Create the SVX node if required
  PHCompositeNode* svxNode 
    = dynamic_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode","SVTX"));
  if (!svxNode) {
    svxNode = new PHCompositeNode("SVTX");
    dstNode->addNode(svxNode);
  }
  
  // Create the Cluster node if required
  SvtxClusterMap *svxclusters 
    = findNode::getClass<SvtxClusterMap>(topNode,"SvtxClusterMap");
  if (!svxclusters) {
    svxclusters = new SvtxClusterMap_v1();
    PHIODataNode<PHObject> *SvtxClusterMapNode =
      new PHIODataNode<PHObject>(svxclusters, "SvtxClusterMap", "PHObject");
    svxNode->addNode(SvtxClusterMapNode);
  }

  //---------------------
  // Calculate Thresholds
  //---------------------
  
  CalculateCylinderThresholds(topNode);
  CalculateLadderThresholds(topNode);

  //----------------
  // Report Settings
  //----------------

  if (verbosity > 0) {
    cout << "====================== PHG4SvtxClusterizer::InitRun() =====================" << endl;
    cout << " Fraction of expected thickness MIP energy = " << _fraction_of_mip << endl;
    for (std::map<int,float>::iterator iter = _thresholds_by_layer.begin();
	 iter != _thresholds_by_layer.end();
	 ++iter) {
      cout << " Cluster Threshold in Layer #" << iter->first << " = " << 1.0e6*iter->second << " keV" << endl;
    }
    for (std::map<int,bool>::iterator iter = _make_z_clustering.begin();
	 iter != _make_z_clustering.end();
	 ++iter) {
      cout << " Z-dimension Clustering in Layer #" << iter->first << " = " << boolalpha << iter->second << noboolalpha << endl;
    }
    for (std::map<int,bool>::iterator iter = _make_e_weights.begin();
	 iter != _make_e_weights.end();
	 ++iter) {
      cout << " Energy weighting clusters in Layer #" << iter->first << " = " << boolalpha << iter->second << noboolalpha << endl;
    }
    cout << "===========================================================================" << endl;
  }

  return Fun4AllReturnCodes::EVENT_OK;
}

int PHG4SvtxClusterizer::process_event(PHCompositeNode *topNode) {

  _timer.get()->restart();
  
  _clusterlist = findNode::getClass<SvtxClusterMap>(topNode,"SvtxClusterMap");
  if (!_clusterlist) 
    {
      cout << PHWHERE << " ERROR: Can't find SvtxClusterMap." << endl;
      return Fun4AllReturnCodes::ABORTRUN;
    }
  _clusterlist->Reset();
  
  ClusterCylinderCells(topNode);
  ClusterLadderCells(topNode);

  PrintClusters(topNode);
  
  _timer.get()->stop();
  return Fun4AllReturnCodes::EVENT_OK;
}

void PHG4SvtxClusterizer::CalculateCylinderThresholds(PHCompositeNode *topNode) {

  // get the SVX geometry object
  PHG4CylinderCellGeomContainer* geom_container = findNode::getClass<PHG4CylinderCellGeomContainer>(topNode,"CYLINDERCELLGEOM_SVTX");
  if (!geom_container) return;
  
  // determine cluster thresholds and layer index mapping
  PHG4CylinderCellGeomContainer::ConstRange layerrange = geom_container->get_begin_end();
  for(PHG4CylinderCellGeomContainer::ConstIterator layeriter = layerrange.first;
      layeriter != layerrange.second;
      ++layeriter) {

    // index mapping
    int layer = layeriter->second->get_layer();

    // cluster threshold
    float thickness = (layeriter->second)->get_thickness();
    float threshold = _fraction_of_mip*0.003876*thickness;
    _thresholds_by_layer.insert(std::make_pair(layer,threshold));

    // fill in a default z_clustering value if not present
    if (_make_z_clustering.find(layer) == _make_z_clustering.end()) {
      _make_z_clustering.insert(std::make_pair(layer,true));
    }
    
    if (_make_e_weights.find(layer) == _make_e_weights.end()) {
      _make_e_weights.insert(std::make_pair(layer,false));
    }
  }
  
  return;
}

void PHG4SvtxClusterizer::CalculateLadderThresholds(PHCompositeNode *topNode) {

  PHG4CylinderCellContainer *cells = findNode::getClass<PHG4CylinderCellContainer>(topNode,"G4CELL_SILICON_TRACKER");
  if (!cells) return;

  PHG4CylinderGeomContainer *geom_container = findNode::getClass<PHG4CylinderGeomContainer>(topNode,"CYLINDERGEOM_SILICON_TRACKER");
  if (!geom_container) return;
  
  PHG4CylinderGeomContainer::ConstRange layerrange = geom_container->get_begin_end();
  for(PHG4CylinderGeomContainer::ConstIterator layeriter = layerrange.first;
      layeriter != layerrange.second;
      ++layeriter) {

    // index mapping
    int layer = layeriter->second->get_layer();

    // cluster threshold
    float thickness = (layeriter->second)->get_thickness();
    float threshold = _fraction_of_mip*0.003876*thickness;
    _thresholds_by_layer.insert(std::make_pair(layer,threshold));

    // fill in a default z_clustering value if not present
    if (_make_z_clustering.find(layer) == _make_z_clustering.end()) {
      _make_z_clustering.insert(std::make_pair(layer,true));
    }

    if (_make_e_weights.find(layer) == _make_e_weights.end()) {
      _make_e_weights.insert(std::make_pair(layer,false));
    }
  }
  
  return;
}

void PHG4SvtxClusterizer::ClusterCylinderCells(PHCompositeNode *topNode) {

  //----------
  // Get Nodes
  //----------

  // get the SVX geometry object
  PHG4CylinderCellGeomContainer* geom_container = findNode::getClass<PHG4CylinderCellGeomContainer>(topNode,"CYLINDERCELLGEOM_SVTX");
  if (!geom_container) return;
  
  PHG4HitContainer* g4hits = findNode::getClass<PHG4HitContainer>(topNode,"G4HIT_SVTX");
  if (!g4hits) return;
  
  PHG4CylinderCellContainer* cells = findNode::getClass<PHG4CylinderCellContainer>(topNode,"G4CELL_SVTX");
  if (!cells) return; 
  
  //-----------
  // Clustering
  //-----------

  // sort hits layer by layer
  std::multimap<int,SvtxHit*> layer_hits_mmap;  
  for (SvtxHitMap::Iter iter = _hits->begin();
       iter != _hits->end();
       ++iter) {
    SvtxHit* hit = iter->second;
    layer_hits_mmap.insert(make_pair(hit->get_layer(),hit));
  }
  
  // loop over cylinder layers
  PHG4CylinderCellGeomContainer::ConstRange layerrange = geom_container->get_begin_end();
  for(PHG4CylinderCellGeomContainer::ConstIterator layeriter = layerrange.first;
      layeriter != layerrange.second;
      ++layeriter) {

    int layer = layeriter->second->get_layer();

    if ((unsigned int)layer < _min_layer) continue;
    if ((unsigned int)layer > _max_layer) continue;
    
    int nphibins = layeriter->second->get_phibins();

    // loop over all hits/cells in this layer
    std::map<PHG4CylinderCell*,SvtxHit*> cell_hit_map;
    std::vector<PHG4CylinderCell*> cell_list;   
    for (std::multimap<int,SvtxHit*>::iterator hiter = layer_hits_mmap.lower_bound(layer);
	 hiter != layer_hits_mmap.upper_bound(layer);
	 ++hiter) {
      SvtxHit* hit = hiter->second;
      PHG4CylinderCell* cell = cells->findCylinderCell(hit->get_cellid());
      cell_list.push_back(cell);
      cell_hit_map.insert(make_pair(cell,hit));
    }

    if (cell_list.size() == 0) continue; // if no cells, go to the next layer
    
    sort(cell_list.begin(), cell_list.end(), PHG4SvtxClusterizer::lessthan);

    typedef adjacency_list <vecS, vecS, undirectedS> Graph;
    Graph G;

    for(unsigned int i=0; i<cell_list.size(); i++) {
      for(unsigned int j=i+1; j<cell_list.size(); j++) {
        if( are_adjacent(cell_list[i], cell_list[j], nphibins) )
          add_edge(i,j,G);
      }
      
      add_edge(i,i,G);
    }

    // Find the connections between the vertices of the graph (vertices are the rawhits, 
    // connections are made when they are adjacent to one another)
    vector<int> component(num_vertices(G));
    
    // this is the actual clustering, performed by boost
    connected_components(G, &component[0]); 

    // Loop over the components(hit cells) compiling a list of the
    // unique connected groups (ie. clusters).
    set<int> cluster_ids; // unique components       
    multimap<int, PHG4CylinderCell*> clusters;
    for (unsigned int i=0; i<component.size(); i++) {
      cluster_ids.insert( component[i] );
      clusters.insert( make_pair(component[i], cell_list[i]) );
    }
    
    typedef multimap<int, PHG4CylinderCell*>::iterator mapiterator;
    
    for (set<int>::iterator clusiter = cluster_ids.begin(); 
	 clusiter != cluster_ids.end(); 
	 clusiter++ ) {
      
      int clusid = *clusiter;
      pair<mapiterator,mapiterator> clusrange = clusters.equal_range(clusid);
      
      mapiterator mapiter = clusrange.first;
      
      int layer = mapiter->second->get_layer();
      PHG4CylinderCellGeom* geom = geom_container->GetLayerCellGeom(layer);
      
      SvtxCluster_v1 clus;
      clus.set_layer( layer );
      float clus_energy = 0.0;
      unsigned int clus_adc = 0;

      // determine the size of the cluster in phi and z
      // useful for track fitting the cluster

      set<int> phibins;
      set<int> zbins;
      for (mapiter = clusrange.first; mapiter != clusrange.second; mapiter++ ) {
	PHG4CylinderCell* cell = mapiter->second;     
	
	phibins.insert(cell->get_binphi());
	zbins.insert(cell->get_binz());
      }
      
      float pitch = geom->get_phistep()*geom->get_radius();
      float thickness = geom->get_thickness();
      float length = geom->get_zstep();
      float phisize = phibins.size()*pitch;
      float zsize = zbins.size()*length;

      double xsum = 0.0;
      double ysum = 0.0;
      double zsum = 0.0;
      unsigned int nhits = 0;

      for(mapiter = clusrange.first; mapiter != clusrange.second; mapiter++ ) {
        PHG4CylinderCell* cell = mapiter->second;
	SvtxHit* hit = cell_hit_map[cell];
	
	clus.insert_hit(hit->get_id());
	
        clus_energy += hit->get_e();
	clus_adc    += hit->get_adc();

	// compute the hit center
	double r   = geom->get_radius();
        double phi = geom->get_phicenter(cell->get_binphi());

	double x = r*cos(phi);
	double y = r*sin(phi);
        double z = geom->get_zcenter(cell->get_binz());

	if (_make_e_weights[layer]) {
	  xsum += x * hit->get_adc();
	  ysum += y * hit->get_adc();
	  zsum += z * hit->get_adc();  
	} else {
	  xsum += x;
	  ysum += y;
	  zsum += z;
	}
	++nhits;
      }
      
      double clusx = NAN;
      double clusy = NAN;
      double clusz = NAN;

      if (_make_e_weights[layer]) {
	clusx = xsum / clus_adc;
	clusy = ysum / clus_adc;
	clusz = zsum / clus_adc;	
      } else {
	clusx = xsum / nhits;
	clusy = ysum / nhits;
	clusz = zsum / nhits;
      }
      
      double radius  = sqrt(clusx*clusx+clusy*clusy);
      double clusphi = atan2( clusy, clusx);
       
      clus.set_position( 0 , clusx );
      clus.set_position( 1 , clusy );
      clus.set_position( 2 , clusz );

      clus.set_e(clus_energy);
      clus.set_adc(clus_adc);

      float invsqrt12 = 1.0/sqrt(12.);
      
      TMatrixF DIM(3,3);
      DIM[0][0] = pow(0.5*thickness,2);
      DIM[0][1] = 0.0;
      DIM[0][2] = 0.0;
      DIM[1][0] = 0.0;
      DIM[1][1] = pow(0.5*phisize,2);
      DIM[1][2] = 0.0;
      DIM[2][0] = 0.0;
      DIM[2][1] = 0.0;
      DIM[2][2] = pow(0.5*zsize,2);

      TMatrixF ERR(3,3);
      ERR[0][0] = pow(0.5*thickness*invsqrt12,2);
      ERR[0][1] = 0.0;
      ERR[0][2] = 0.0;
      ERR[1][0] = 0.0;
      ERR[1][1] = pow(0.5*phisize*invsqrt12,2);
      ERR[1][2] = 0.0;
      ERR[2][0] = 0.0;
      ERR[2][1] = 0.0;
      ERR[2][2] = pow(0.5*zsize*invsqrt12,2);

      TMatrixF ROT(3,3);
      ROT[0][0] = cos(clusphi);
      ROT[0][1] = -sin(clusphi);
      ROT[0][2] = 0.0;
      ROT[1][0] = sin(clusphi);
      ROT[1][1] = cos(clusphi);
      ROT[1][2] = 0.0;
      ROT[2][0] = 0.0;
      ROT[2][1] = 0.0;
      ROT[2][2] = 1.0;

      TMatrixF ROT_T(3,3);
      ROT_T.Transpose(ROT);
      
      TMatrixF COVAR_DIM(3,3);
      COVAR_DIM = ROT * DIM * ROT_T;
      
      clus.set_size( 0 , 0 , COVAR_DIM[0][0] );
      clus.set_size( 0 , 1 , COVAR_DIM[0][1] );
      clus.set_size( 0 , 2 , COVAR_DIM[0][2] );
      clus.set_size( 1 , 0 , COVAR_DIM[1][0] );
      clus.set_size( 1 , 1 , COVAR_DIM[1][1] );
      clus.set_size( 1 , 2 , COVAR_DIM[1][2] );
      clus.set_size( 2 , 0 , COVAR_DIM[2][0] );
      clus.set_size( 2 , 1 , COVAR_DIM[2][1] );
      clus.set_size( 2 , 2 , COVAR_DIM[2][2] );

      TMatrixF COVAR_ERR(3,3);
      COVAR_ERR = ROT * ERR * ROT_T;

      clus.set_error( 0 , 0 , COVAR_ERR[0][0] );
      clus.set_error( 0 , 1 , COVAR_ERR[0][1] );
      clus.set_error( 0 , 2 , COVAR_ERR[0][2] );
      clus.set_error( 1 , 0 , COVAR_ERR[1][0] );
      clus.set_error( 1 , 1 , COVAR_ERR[1][1] );
      clus.set_error( 1 , 2 , COVAR_ERR[1][2] );
      clus.set_error( 2 , 0 , COVAR_ERR[2][0] );
      clus.set_error( 2 , 1 , COVAR_ERR[2][1] );
      clus.set_error( 2 , 2 , COVAR_ERR[2][2] );
      
      if (clus_energy > get_threshold_by_layer(layer)) {
	SvtxCluster* ptr = _clusterlist->insert(&clus);
	if (!ptr->isValid()) {
	  static bool first = true;
	  if (first) {
	    cout << PHWHERE << "ERROR: Invalid SvtxClusters are being produced" << endl;
	    ptr->identify();
	    first = false;
	  }
	}
	
	if (verbosity>1) {
	  cout << "r=" << radius << " phi=" << clusphi << " z=" << clusz << endl;
	  cout << "pos=(" << clus.get_position(0) << ", " << clus.get_position(1)
	       << ", " << clus.get_position(2) << ")" << endl;
	  cout << endl;
	}
      }	else if (verbosity>1) {
	cout << "removed r=" << radius << " phi=" << clusphi << " z=" << clusz << endl;
	cout << "pos=(" << clus.get_position(0) << ", " << clus.get_position(1)
	     << ", " << clus.get_position(2) << ")" << endl;
	cout << endl;
      } 
    }
  }
  
  return;
}

void PHG4SvtxClusterizer::ClusterLadderCells(PHCompositeNode *topNode) {

  //----------
  // Get Nodes
  //----------

  // get the SVX geometry object
  PHG4CylinderGeomContainer* geom_container = findNode::getClass<PHG4CylinderGeomContainer>(topNode,"CYLINDERGEOM_SILICON_TRACKER");
  if (!geom_container) return;
  
  PHG4HitContainer* g4hits =  findNode::getClass<PHG4HitContainer>(topNode,"G4HIT_SILICON_TRACKER");
  if (!g4hits) return;
  
  PHG4CylinderCellContainer* cells =  findNode::getClass<PHG4CylinderCellContainer>(topNode,"G4CELL_SILICON_TRACKER");
  if (!cells) return; 
 
  //-----------
  // Clustering
  //-----------

  // sort hits layer by layer
  std::multimap<int,SvtxHit*> layer_hits_mmap;
  for (SvtxHitMap::Iter iter = _hits->begin();
       iter != _hits->end();
       ++iter) {
    SvtxHit* hit = iter->second;
    layer_hits_mmap.insert(make_pair(hit->get_layer(),hit));
  }
  
  PHG4CylinderGeomContainer::ConstRange layerrange = geom_container->get_begin_end();
  for(PHG4CylinderGeomContainer::ConstIterator layeriter = layerrange.first;
      layeriter != layerrange.second;
      ++layeriter) {

    int layer = layeriter->second->get_layer();

    if ((unsigned int)layer < _min_layer) continue;
    if ((unsigned int)layer > _max_layer) continue;
    
    std::map<PHG4CylinderCell*,SvtxHit*> cell_hit_map;
    vector<PHG4CylinderCell*> cell_list;
    for (std::multimap<int,SvtxHit*>::iterator hiter = layer_hits_mmap.lower_bound(layer);
	 hiter != layer_hits_mmap.upper_bound(layer);
	 ++hiter) {
      SvtxHit* hit = hiter->second;
      PHG4CylinderCell* cell = cells->findCylinderCell(hit->get_cellid());
      cell_list.push_back(cell);
      cell_hit_map.insert(make_pair(cell,hit));
    }
    
    if (cell_list.size() == 0) continue; // if no cells, go to the next layer
    
    // i'm not sure this sorting is ever really used
    sort(cell_list.begin(), cell_list.end(), PHG4SvtxClusterizer::ladder_lessthan);

    typedef adjacency_list <vecS, vecS, undirectedS> Graph;
    Graph G;

    for(unsigned int i=0; i<cell_list.size(); i++) {
      for(unsigned int j=i+1; j<cell_list.size(); j++) {
        if(ladder_are_adjacent(cell_list[i], cell_list[j]) )
          add_edge(i,j,G);
      }
      
      add_edge(i,i,G);
    }

    // Find the connections between the vertices of the graph (vertices are the rawhits, 
    // connections are made when they are adjacent to one another)
    vector<int> component(num_vertices(G));
    
    // this is the actual clustering, performed by boost
    connected_components(G, &component[0]); 

    // Loop over the components(hit cells) compiling a list of the
    // unique connected groups (ie. clusters).
    set<int> cluster_ids; // unique components
    multimap<int, PHG4CylinderCell*> clusters;
    for (unsigned int i=0; i<component.size(); i++) {
      cluster_ids.insert( component[i] );
      clusters.insert( make_pair(component[i], cell_list[i]) );
    }
    
    // 
    for (set<int>::iterator clusiter = cluster_ids.begin(); 
	 clusiter != cluster_ids.end(); 
	 clusiter++ ) {
      
      int clusid = *clusiter;
      pair<multimap<int, PHG4CylinderCell*>::iterator,
	   multimap<int, PHG4CylinderCell*>::iterator> clusrange = clusters.equal_range(clusid);
      
      multimap<int, PHG4CylinderCell*>::iterator mapiter = clusrange.first;
      
      int layer = mapiter->second->get_layer();
      PHG4CylinderGeom* geom = geom_container->GetLayerGeom(layer);
      
      SvtxCluster_v1 clus;
      clus.set_layer( layer );
      float clus_energy = 0.0;
      unsigned int clus_adc = 0;

      // determine the size of the cluster in phi and z
      // useful for track fitting the cluster

      set<int> phibins;
      set<int> zbins;
      for (mapiter = clusrange.first; mapiter != clusrange.second; mapiter++ ) {
	PHG4CylinderCell* cell = mapiter->second;     
	
	phibins.insert(cell->get_binphi());
	zbins.insert(cell->get_binz());
      }

      float thickness = geom->get_thickness();
      float pitch = geom->get_strip_y_spacing();
      float length = geom->get_strip_z_spacing();
      float phisize = phibins.size()*pitch;
      float zsize = zbins.size()*length;
      float tilt = geom->get_strip_tilt();

      // determine the cluster position...
      double xsum = 0.0;
      double ysum = 0.0;
      double zsum = 0.0;
      unsigned nhits = 0;

      int ladder_z_index = -1;
      int ladder_phi_index = -1;
      
      for(mapiter = clusrange.first; mapiter != clusrange.second; mapiter++ ) {
        PHG4CylinderCell* cell = mapiter->second;
	SvtxHit* hit = cell_hit_map[cell];
	
	clus.insert_hit(hit->get_id());
	
        clus_energy += hit->get_e();
	clus_adc    += hit->get_adc();

	double hit_location[3] = {0.0,0.0,0.0};
	geom->find_strip_center(cell->get_ladder_z_index(),
				cell->get_ladder_phi_index(),
				cell->get_binz(),
				cell->get_binphi(),
				hit_location);

	ladder_z_index = cell->get_ladder_z_index();
	ladder_phi_index = cell->get_ladder_phi_index();

	if (_make_e_weights[layer]) {
	  xsum += hit_location[0] * hit->get_adc();
	  ysum += hit_location[1] * hit->get_adc();
	  zsum += hit_location[2] * hit->get_adc();  
	} else {
	  xsum += hit_location[0];
	  ysum += hit_location[1];
	  zsum += hit_location[2];
	}
	++nhits;
      }

      double clusx = NAN;
      double clusy = NAN;
      double clusz = NAN;

      if (_make_e_weights[layer]) {
	clusx = xsum / clus_adc;
	clusy = ysum / clus_adc;
	clusz = zsum / clus_adc;	
      } else {
	clusx = xsum / nhits;
	clusy = ysum / nhits;
	clusz = zsum / nhits;
      }
      
      double ladder_location[3] = {0.0,0.0,0.0};
      geom->find_segment_center(ladder_z_index,
				ladder_phi_index,
				ladder_location);
      double ladderphi = atan2( ladder_location[1], ladder_location[0] );
      
      clus.set_position(0, clusx);
      clus.set_position(1, clusy);
      clus.set_position(2, clusz);

      clus.set_e(clus_energy);
      clus.set_adc(clus_adc);

      float invsqrt12 = 1.0/sqrt(12.0);
      
      TMatrixF DIM(3,3);
      DIM[0][0] = pow(0.5*thickness,2);
      DIM[0][1] = 0.0;
      DIM[0][2] = 0.0;
      DIM[1][0] = 0.0;
      DIM[1][1] = pow(0.5*phisize,2);
      DIM[1][2] = 0.0;
      DIM[2][0] = 0.0;
      DIM[2][1] = 0.0;
      DIM[2][2] = pow(0.5*zsize,2);

      TMatrixF ERR(3,3);
      ERR[0][0] = pow(0.5*thickness*invsqrt12,2);
      ERR[0][1] = 0.0;
      ERR[0][2] = 0.0;
      ERR[1][0] = 0.0;
      ERR[1][1] = pow(0.5*phisize*invsqrt12,2);
      ERR[1][2] = 0.0;
      ERR[2][0] = 0.0;
      ERR[2][1] = 0.0;
      ERR[2][2] = pow(0.5*zsize*invsqrt12,2);
      
      TMatrixF ROT(3,3);
      ROT[0][0] = cos(ladderphi);
      ROT[0][1] = -1.0*sin(ladderphi);
      ROT[0][2] = 0.0;
      ROT[1][0] = sin(ladderphi);
      ROT[1][1] = cos(ladderphi);
      ROT[1][2] = 0.0;
      ROT[2][0] = 0.0;
      ROT[2][1] = 0.0;
      ROT[2][2] = 1.0;

      TMatrixF TILT(3,3);
      TILT[0][0] = 1.0;
      TILT[0][1] = 0.0;
      TILT[0][2] = 0.0;
      TILT[1][0] = 0.0;
      TILT[1][1] = cos(tilt);
      TILT[1][2] = -1.0*sin(tilt);
      TILT[2][0] = 0.0;
      TILT[2][1] = sin(tilt);
      TILT[2][2] = cos(tilt);

      TMatrixF R(3,3);
      R = ROT * TILT;
      
      TMatrixF R_T(3,3);
      R_T.Transpose(R);
          
      TMatrixF COVAR_DIM(3,3);
      COVAR_DIM = R * DIM * R_T;
      
      clus.set_size( 0 , 0 , COVAR_DIM[0][0] );
      clus.set_size( 0 , 1 , COVAR_DIM[0][1] );
      clus.set_size( 0 , 2 , COVAR_DIM[0][2] );
      clus.set_size( 1 , 0 , COVAR_DIM[1][0] );
      clus.set_size( 1 , 1 , COVAR_DIM[1][1] );
      clus.set_size( 1 , 2 , COVAR_DIM[1][2] );
      clus.set_size( 2 , 0 , COVAR_DIM[2][0] );
      clus.set_size( 2 , 1 , COVAR_DIM[2][1] );
      clus.set_size( 2 , 2 , COVAR_DIM[2][2] );

      TMatrixF COVAR_ERR(3,3);
      COVAR_ERR = R * ERR * R_T;
      
      clus.set_error( 0 , 0 , COVAR_ERR[0][0] );
      clus.set_error( 0 , 1 , COVAR_ERR[0][1] );
      clus.set_error( 0 , 2 , COVAR_ERR[0][2] );
      clus.set_error( 1 , 0 , COVAR_ERR[1][0] );
      clus.set_error( 1 , 1 , COVAR_ERR[1][1] );
      clus.set_error( 1 , 2 , COVAR_ERR[1][2] );
      clus.set_error( 2 , 0 , COVAR_ERR[2][0] );
      clus.set_error( 2 , 1 , COVAR_ERR[2][1] );
      clus.set_error( 2 , 2 , COVAR_ERR[2][2] );
      
      if (clus_energy > get_threshold_by_layer(layer)) {
	SvtxCluster* ptr = _clusterlist->insert(&clus);
	if (!ptr->isValid()) {
	  static bool first = true;
	  if (first) {
	    cout << PHWHERE << "ERROR: Invalid SvtxClusters are being produced" << endl;
	    ptr->identify();
	    first = false;
	  }
	}
	
	if (verbosity>1) {
	  double radius = sqrt(clusx*clusx+clusy*clusy);
	  double clusphi = atan2(clusy,clusx);
	  cout << "r=" << radius << " phi=" << clusphi << " z=" << clusz << endl;
	  cout << "pos=(" << clus.get_position(0) << ", " << clus.get_position(1)
	       << ", " << clus.get_position(2) << ")" << endl;
	  cout << endl;
	}
      }	else if (verbosity>1) {
	double radius = sqrt(clusx*clusx+clusy*clusy);
	double clusphi = atan2(clusy,clusx);
	cout << "removed r=" << radius << " phi=" << clusphi << " z=" << clusz << endl;
	cout << "pos=(" << clus.get_position(0) << ", " << clus.get_position(1)
	     << ", " << clus.get_position(2) << ")" << endl;
	cout << endl;
      } 
    }
  }
  
  return;
}

void PHG4SvtxClusterizer::PrintClusters(PHCompositeNode *topNode) {

  if (verbosity >= 1) {

    SvtxClusterMap *clusterlist = findNode::getClass<SvtxClusterMap>(topNode,"SvtxClusterMap");
    if (!clusterlist) return;
    
    cout << "================= PHG4SvtxClusterizer::process_event() ====================" << endl;
  

    cout << " Found and recorded the following " << clusterlist->size() << " clusters: " << endl;

    unsigned int icluster = 0;
    for (SvtxClusterMap::Iter iter = clusterlist->begin();
	 iter != clusterlist->end();
	 ++iter) {

      SvtxCluster* cluster = iter->second;
      cout << icluster << " of " << clusterlist->size() << endl;
      cluster->identify();
      ++icluster;
    }
    
    cout << "===========================================================================" << endl;
  }
  
  return;
}
