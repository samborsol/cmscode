#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "DataFormats/GeometryVector/interface/LocalPoint.h"
#include "DataFormats/GeometryVector/interface/GlobalPoint.h"
#include "DataFormats/SiPixelDetId/interface/PXBDetId.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/TrackerGeometryBuilder/interface/PixelGeomDetUnit.h"
#include "Geometry/CommonTopologies/interface/PixelTopology.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/CommonDetUnit/interface/GeomDet.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"

#include "TTree.h"
#include "TFile.h"

//
// class declaration
//

class UPCPixelClusterShapeAnalyzer : public edm::EDAnalyzer{
public:
  explicit UPCPixelClusterShapeAnalyzer(const edm::ParameterSet&);
  ~UPCPixelClusterShapeAnalyzer();

private:

  edm::InputTag       inputTag_;      // input tag identifying product containing pixel clusters
  double              minZ_;          // beginning z-vertex position
  double              maxZ_;          // end z-vertex position
  double              zStep_;         // size of steps in z-vertex test

  std::vector<double> clusterPars_;   //pixel cluster polynomial pars for vertex compatibility cut
  int                 nhitsTrunc_;    //maximum pixel clusters to apply compatibility check
  int                 nPxlHits;
  double              clusterTrunc_;  //maximum vertex compatibility value for event rejection
  bool                accept;

  struct VertexHit
  {
    float z;
    float r;
    float w;
  };

  virtual void beginJob();
  virtual void analyze(const edm::Event&, const edm::EventSetup&);
  int getContainedHits(const std::vector<VertexHit> &hits, double z0, double &chi);

  edm::Service<TFileService> mFileServer;
  TTree* ClusterShapeTree; 

};

//
// constructors and destructor
//
 
UPCPixelClusterShapeAnalyzer::UPCPixelClusterShapeAnalyzer(const edm::ParameterSet& config) :
  inputTag_     (config.getParameter<edm::InputTag>("inputTag")),
  minZ_         (config.getParameter<double>("minZ")),
  maxZ_         (config.getParameter<double>("maxZ")),
  zStep_        (config.getParameter<double>("zStep")),
  clusterPars_  (config.getParameter< std::vector<double> >("clusterPars")),
  nhitsTrunc_   (config.getParameter<int>("nhitsTrunc")),
  clusterTrunc_ (config.getParameter<double>("clusterTrunc"))
{
  LogDebug("") << "Using the " << inputTag_ << " input collection";
}

UPCPixelClusterShapeAnalyzer::~UPCPixelClusterShapeAnalyzer()
{
}

void UPCPixelClusterShapeAnalyzer::beginJob(){
  mFileServer->file().cd();

  ClusterShapeTree = new TTree("ClusterShapeTree","ClusterShapeTree");

  ClusterShapeTree->Branch("Accept",&accept,"Accept/O");
  ClusterShapeTree->Branch("nPxlHits",&nPxlHits,"nPxlHits/I");
}
//
// member functions
//

// ------------ method called to produce the data  ------------
void UPCPixelClusterShapeAnalyzer::analyze(const edm::Event& event, const edm::EventSetup& iSetup)
{
  // All HLT filters must create and fill an HLT filter object,
  // recording any reconstructed physics objects satisfying (or not)
  // this HLT filter, and place it in the Event.

  // The filter object
  accept = true;

  // get hold of products from Event
  edm::Handle<SiPixelRecHitCollection> hRecHits;
  event.getByLabel(inputTag_, hRecHits);

  // get tracker geometry
  if (hRecHits.isValid()) {
    edm::ESHandle<TrackerGeometry> trackerHandle;
    iSetup.get<TrackerDigiGeometryRecord>().get(trackerHandle);
    const TrackerGeometry *tgeo = trackerHandle.product();
    const SiPixelRecHitCollection *hits = hRecHits.product();

    // loop over pixel rechits
    nPxlHits=0;
    std::vector<VertexHit> vhits;
    for(SiPixelRecHitCollection::DataContainer::const_iterator hit = hits->data().begin(), 
          end = hits->data().end(); hit != end; ++hit) {
      if (!hit->isValid())
        continue;
      ++nPxlHits;
      DetId id(hit->geographicalId());
      if(id.subdetId() != int(PixelSubdetector::PixelBarrel))
        continue;
      const PixelGeomDetUnit *pgdu = static_cast<const PixelGeomDetUnit*>(tgeo->idToDet(id));
      if (1) {
        const PixelTopology *pixTopo = &(pgdu->specificTopology());
        std::vector<SiPixelCluster::Pixel> pixels(hit->cluster()->pixels());
        bool pixelOnEdge = false;
        for(std::vector<SiPixelCluster::Pixel>::const_iterator pixel = pixels.begin(); 
            pixel != pixels.end(); ++pixel) {
          int pixelX = pixel->x;
          int pixelY = pixel->y;
          if(pixTopo->isItEdgePixelInX(pixelX) || pixTopo->isItEdgePixelInY(pixelY)) {
            pixelOnEdge = true;
            break;
          }
        }
        if (pixelOnEdge)
          continue;
      }

      LocalPoint lpos = LocalPoint(hit->localPosition().x(),
				   hit->localPosition().y(),
				   hit->localPosition().z());
      GlobalPoint gpos = pgdu->toGlobal(lpos);
      VertexHit vh;
      vh.z = gpos.z(); 
      vh.r = gpos.perp(); 
      vh.w = hit->cluster()->sizeY();
      vhits.push_back(vh);
    }

    // estimate z-position from cluster lengths
    double zest = 0.0;
    int nhits = 0, nhits_max = 0;
    double chi = 0, chi_max = 1e+9;
    for(double z0 = minZ_; z0 <= maxZ_; z0 += zStep_) {
      nhits = getContainedHits(vhits, z0, chi);
      if(nhits == 0) 
	continue;
      if(nhits > nhits_max) { 
	chi_max = 1e+9; 
	nhits_max = nhits; 
      }
      if(nhits >= nhits_max && chi < chi_max) {
	chi_max = chi; 
	zest = z0;   
      }
    } 

    chi = 0;
    int nbest=0, nminus=0, nplus=0;
    nbest = getContainedHits(vhits,zest,chi);
    nminus = getContainedHits(vhits,zest-10.,chi);
    nplus = getContainedHits(vhits,zest+10.,chi);

    double clusVtxQual=0.0;
    if ((nminus+nplus)> 0)
      clusVtxQual = (2.0*nbest)/(nminus+nplus);  // A/B
    else if (nbest>0)
      clusVtxQual = 1000.0;                      // A/0 (set to arbitrarily large number)
    else
      clusVtxQual = 0;                           // 0/0 (already the default)

  // construct polynomial cut on cluster vertex quality vs. npixelhits
  double polyCut=0;
  for(unsigned int i=0; i < clusterPars_.size(); i++) {
    polyCut += clusterPars_[i]*std::pow((double)nPxlHits,(int)i);
  }
  if(nPxlHits < nhitsTrunc_) 
    polyCut=0;             // don't use cut below nhitsTrunc_ pixel hits
  if(polyCut > clusterTrunc_ && clusterTrunc_ > 0) 
    polyCut=clusterTrunc_; // no cut above clusterTrunc_

  if (clusVtxQual < polyCut) accept = false;
  }

  // return with final filter decision
  ClusterShapeTree->Fill();
}

int UPCPixelClusterShapeAnalyzer::getContainedHits(const std::vector<VertexHit> &hits, double z0, double &chi)
{
  // Calculate number of hits contained in v-shaped window in cluster y-width vs. z-position.
  int n = 0;
  chi   = 0.;

  for(std::vector<VertexHit>::const_iterator hit = hits.begin(); hit!= hits.end(); hit++) {
    double p = 2 * fabs(hit->z - z0)/hit->r + 0.5; // FIXME
    if(fabs(p - hit->w) <= 1.) { 
      chi += fabs(p - hit->w);
      n++;
    }
  }
  return n;
}


// define as a framework module
#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(UPCPixelClusterShapeAnalyzer);
