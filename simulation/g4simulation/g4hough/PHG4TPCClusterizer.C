#include "PHG4TPCClusterizer.h"
#include "SvtxCluster.h"
#include "SvtxClusterMap.h"
#include "SvtxClusterMap_v1.h"
#include "SvtxCluster_v1.h"
#include "SvtxHit.h"
#include "SvtxHitMap.h"

#include <fun4all/Fun4AllReturnCodes.h>
#include <g4detectors/PHG4CylinderCell.h>
#include <g4detectors/PHG4CylinderCellContainer.h>
#include <g4detectors/PHG4CylinderCellGeom.h>
#include <g4detectors/PHG4CylinderCellGeomContainer.h>
#include <g4detectors/PHG4CylinderGeom.h>
#include <g4detectors/PHG4CylinderGeomContainer.h>
#include <g4main/PHG4Hit.h>
#include <g4main/PHG4HitContainer.h>
#include <phool/PHCompositeNode.h>
#include <phool/PHIODataNode.h>
#include <phool/PHNodeIterator.h>
#include <phool/PHTypedNodeIterator.h>
#include <phool/getClass.h>

#include <TMath.h>

#include <TF1.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TH1D.h>

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace std;

static int phi_span = 10;
static int z_span = 10;

static int wrap_bin(int bin, int nbins) {
  if (bin < 0) {
    bin += nbins;
  }
  if (bin >= nbins) {
    bin -= nbins;
  }
  return bin;
}

static bool is_local_maximum(const std::vector<float>& amps, int nphibins,
                             int nzbins, int phi, int z) {
  int max_width = 32;
  if (amps[z * nphibins + phi] <= 0.) {
    return false;
  }
  float cent_val = amps[z * nphibins + phi];
  bool is_max = true;
  for (int iz = -max_width; iz <= max_width; ++iz) {
    int cz = z + iz;
    if (cz < 0) {
      continue;
    }
    if (cz >= (int)(nzbins)) {
      continue;
    }
    for (int ip = -max_width; ip <= max_width; ++ip) {
      if ((iz == 0) && (ip == 0)) {
        continue;
      }
      int cp = wrap_bin(phi + ip, nphibins);
      assert(cp >= 0);
      if (amps[cz * nphibins + cp] > cent_val) {
        is_max = false;
        break;
      }
    }
    if (is_max == false) {
      break;
    }
  }
  return is_max;
}

static void fit_cluster(std::vector<float>& amps, int nphibins, int nzbins,
                        int& nhits_tot, std::vector<int>& nhits, int phibin,
                        int zbin, PHG4CylinderCellGeom* geo, float& phi,
                        float& z, float& e) {
  e = 0.;
  phi = 0.;
  z = 0.;
  
  float prop_cut = 0.05;
  float peak = amps[zbin * nphibins + phibin];

  for (int iz = -z_span; iz <= z_span; ++iz) {
    int cz = zbin + iz;
    if (cz < 0) {
      continue;
    }
    if (cz >= (int)(nzbins)) {
      continue;
    }
    for (int ip = -phi_span; ip <= phi_span; ++ip) {
      int cp = wrap_bin(phibin + ip, nphibins);
      assert(cp >= 0);
      if (amps[cz * nphibins + cp] <= 0.) {
        continue;
      }
      if (amps[cz * nphibins + cp] < prop_cut * peak) {
        continue;
      }
      e += amps[cz * nphibins + cp];
      phi += amps[cz * nphibins + cp] * geo->get_phicenter(cp);
      z += amps[cz * nphibins + cp] * geo->get_zcenter(cz);
      nhits_tot -= 1;
      nhits[cz] -= 1;
      amps[cz * nphibins + cp] = 0.;
    }
  }

  phi /= e;
  z /= e;
}

int PHG4TPCClusterizer::InitRun(PHCompositeNode* topNode) {
  phi_span = _phi_span;
  z_span = _z_span;
  return Fun4AllReturnCodes::EVENT_OK;
}

void PHG4TPCClusterizer::reset() {}

int PHG4TPCClusterizer::process_event(PHCompositeNode* topNode) {
  
  PHNodeIterator iter(topNode);

  PHCompositeNode* dstNode =
      static_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode", "DST"));
  if (!dstNode) {
    cout << PHWHERE << "DST Node missing, doing nothing." << endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  SvtxHitMap* hits = findNode::getClass<SvtxHitMap>(topNode, "SvtxHitMap");
  if (!hits) {
    cout << PHWHERE << "ERROR: Can't find node SvtxHitMap" << endl;
    return Fun4AllReturnCodes::ABORTRUN;
  }

  PHCompositeNode* svxNode =
      dynamic_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode", "SVTX"));
  if (!svxNode) {
    svxNode = new PHCompositeNode("SVTX");
    dstNode->addNode(svxNode);
  }

  SvtxClusterMap* svxclusters =
      findNode::getClass<SvtxClusterMap>(topNode, "SvtxClusterMap");
  if (!svxclusters) {
    svxclusters = new SvtxClusterMap_v1();
    PHIODataNode<PHObject>* SvtxClusterMapNode =
        new PHIODataNode<PHObject>(svxclusters, "SvtxClusterMap", "PHObject");
    svxNode->addNode(SvtxClusterMapNode);
  }

  PHG4CylinderCellGeomContainer* geom_container =
    findNode::getClass<PHG4CylinderCellGeomContainer>(topNode,"CYLINDERCELLGEOM_SVTX");
  if (!geom_container) return Fun4AllReturnCodes::ABORTRUN;

  PHG4CylinderCellContainer* cells =  findNode::getClass<PHG4CylinderCellContainer>(topNode,"G4CELL_SVTX");
  if (!cells) return Fun4AllReturnCodes::ABORTRUN;

  std::vector<std::vector<const SvtxHit*> > layer_sorted;
  PHG4CylinderCellGeomContainer::ConstRange layerrange = geom_container->get_begin_end();
  for (PHG4CylinderCellGeomContainer::ConstIterator layeriter = layerrange.first;
       layeriter != layerrange.second;
       ++layeriter) {
    layer_sorted.push_back(std::vector<const SvtxHit*>());
  }
  for (SvtxHitMap::Iter iter = hits->begin(); iter != hits->end(); ++iter) {
    SvtxHit* hit = iter->second;
    layer_sorted[hit->get_layer()].push_back(hit);
  }

  for (PHG4CylinderCellGeomContainer::ConstIterator layeriter =
           layerrange.first;
       layeriter != layerrange.second; ++layeriter) {

    unsigned int layer = (unsigned int)layeriter->second->get_layer();
    
    // exit on the MAPS layers...
    if (layer < _min_layer) continue;
    if (layer > _max_layer) continue;
    
    PHG4CylinderCellGeom* geo = geom_container->GetLayerCellGeom(layer);
    nphibins = layeriter->second->get_phibins();
    nzbins = layeriter->second->get_zbins();

    nhits.clear();
    nhits.assign(nzbins, 0);
    amps.clear();
    amps.assign(nphibins * nzbins, 0.);
    cellids.clear();
    cellids.assign(nphibins * nzbins, 0);

    for (unsigned int i = 0; i < layer_sorted[layer].size(); ++i) {

      const SvtxHit* hit = layer_sorted[layer][i];
      if (hit->get_e() <= 0.) continue;
      
      PHG4CylinderCell* cell = cells->findCylinderCell(hit->get_cellid());
      int phibin = cell->get_binphi();
      int zbin = cell->get_binz();
      nhits[zbin] += 1;
      amps[zbin * nphibins + phibin] += hit->get_e();
      cellids[zbin * nphibins + phibin] = hit->get_id();
    }

    int nhits_tot = 0;
    for (int zbin = 0; zbin < nzbins; ++zbin) {
      nhits_tot += nhits[zbin];
    }

    while (nhits_tot > 0) {

      for (int zbin = 0; zbin < nzbins; ++zbin) {

        if (nhits[zbin] <= 0) continue;

        for (int phibin = 0; phibin < nphibins; ++phibin) {

          if (is_local_maximum(amps, nphibins, nzbins, phibin, zbin) == false) {
            continue;
          }

          float phi = 0.;
          float z = 0.;
          float e = 0.;

          fit_cluster(amps, nphibins, nzbins, nhits_tot, nhits, phibin, zbin,
                      geo, phi, z, e);

          if ((layer > 2) && (e < energy_cut)) {
            continue;
          }

          SvtxCluster_v1 clus;
          clus.set_layer(layer);
          clus.set_e(e);
          double radius = geo->get_radius();
          clus.set_position(0, radius * cos(phi));
          clus.set_position(1, radius * sin(phi));
          clus.set_position(2, z);
          clus.insert_hit(cellids[zbin * nphibins + phibin]);

          svxclusters->insert(&clus);
        }
      }
    }
  }

  reset();
  return Fun4AllReturnCodes::EVENT_OK;
}
