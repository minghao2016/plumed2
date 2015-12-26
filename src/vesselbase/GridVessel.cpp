/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2012 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.0.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
    
   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "GridVessel.h"
#include "ActionWithVessel.h"
#include "tools/Tools.h"

namespace PLMD {
namespace vesselbase {

void GridVessel::registerKeywords( Keywords& keys ){
  Vessel::registerKeywords( keys );
  keys.add("compulsory","COMPONENTS","the names of the components in the vector");
  keys.add("compulsory","COORDINATES","the names of the coordinates of the grid");
  keys.add("compulsory","PBC","is the grid periodic in each direction or not");
  keys.add("compulsory","NBINS","number of bins in each direction for the grid");
  keys.addFlag("NOMEMORY",false,"should the data in the grid be deleted after print/analysis");
}

GridVessel::GridVessel( const VesselOptions& da ):
Vessel(da),
bounds_set(false),
bold(0),
cube_units(1.0)
{
  std::vector<std::string> compnames; parseVector("COMPONENTS",compnames);
  std::vector<std::string> coordnames; parseVector("COORDINATES",coordnames);
  dimension=coordnames.size();
  std::vector<std::string> spbc( dimension ); parseVector("PBC",spbc); 
  str_min.resize( dimension);  str_max.resize( dimension ); 
  nbin.resize( dimension ); parseVector("NBINS",nbin);
  parseFlag("NOMEMORY",nomemory);

  tmp_indices.resize( str_min.size() );
  current_neigh.resize( static_cast<unsigned>(pow(2.,dimension)) );

  unsigned n=0; nper=compnames.size()*( 1 + coordnames.size() );
  arg_names.resize( coordnames.size() + compnames.size()*( 1 + coordnames.size() ) );
  for(unsigned i=0;i<coordnames.size();++i){ arg_names[n] = coordnames[i]; n++; }
  for(unsigned i=0;i<compnames.size();++i){
      arg_names[n]=compnames[i]; n++;
      for(unsigned j=0;j<coordnames.size();++j){ arg_names[n] = "d" + compnames[i] + "_" + coordnames[j]; n++; }
  }

  npoints=1; dx.resize( dimension ); pbc.resize( dimension ); 
  min.resize( dimension ); stride.resize( dimension ); max.resize( dimension ); 
  for(unsigned i=0;i<dimension;++i){
      if( spbc[i]=="F" ){ pbc[i]=false; nbin[i]+=1; }  
      else if( spbc[i]=="T" ) pbc[i]=true;
      else plumed_error();
      stride[i]=npoints;
      npoints*=nbin[i];
  }
}

void GridVessel::setBounds( const std::vector<std::string>& smin, const std::vector<std::string>& smax ){
  bounds_set=true; plumed_dbg_assert( smin.size()==dimension && smax.size()==dimension );
  for(unsigned i=0;i<dimension;++i){
      str_min[i]=smin[i]; str_max[i]=smax[i];
      Tools::convert( str_min[i], min[i] );
      Tools::convert( str_max[i], max[i] );
      if( !pbc[i] ){ nbin[i]-=1; }
      dx[i] = ( max[i] - min[i] ) / static_cast<double>( nbin[i] );
      if( !pbc[i] ){ max[i] +=dx[i]; nbin[i]+=1; }
  }
} 

std::string GridVessel::getGridDescription() const {
  std::string des="grid of "; std::string num;
  for(unsigned i=0;i<dimension-1;++i){
      Tools::convert( nbin[i], num );
      des += num + " X ";
  }
  Tools::convert( nbin[dimension-1], num );
  des += num + " equally spaced points between (";
  for(unsigned i=0;i<dimension-1;++i) des += str_min[i] + ",";
  Tools::convert( nbin[dimension-1], num );
  des += str_min[dimension-1] + ") and (";
  for(unsigned i=0;i<dimension-1;++i) des += str_max[i] + ",";
  des += str_max[dimension-1] + ")";
  return des;
}

void GridVessel::resize(){
  plumed_massert( nper>0, "Number of datapoints at each grid point has not been set");
  resizeBuffer( npoints*nper ); data.resize( npoints*nper, 0 );
}

unsigned GridVessel::getIndex( const std::vector<unsigned>& indices ) const {
  plumed_dbg_assert( bounds_set && indices.size()==dimension );
  // indices are flattended using a column-major order
  unsigned index=indices[dimension-1];
  for(unsigned i=dimension-1;i>0;--i){
    index=index*nbin[i-1]+indices[i-1];
  } 
  return index;
}

void GridVessel::convertIndexToIndices( const unsigned& index, const std::vector<unsigned>& nnbin, std::vector<unsigned>& indices ) const {
 unsigned kk=index;
 indices[0]=index%nnbin[0];
 for(unsigned i=1;i<dimension-1;++i){
    kk=(kk-indices[i-1])/nnbin[i-1];
    indices[i]=kk%nnbin[i];
 } 
 if(dimension>=2){  // I think this is wrong
    indices[dimension-1]=(kk-indices[dimension-2])/nnbin[dimension-2];
 }

}

void GridVessel::getIndices( const unsigned& index, std::vector<unsigned>& indices ) const {
 convertIndexToIndices( index, nbin, indices );
}

void GridVessel::getGridPointCoordinates( const unsigned& ipoint , std::vector<double>& x ) const {
  plumed_dbg_assert( x.size()==dimension && ipoint<npoints );
  std::vector<unsigned> tindices( dimension ); getIndices( ipoint, tindices ); 
  for(unsigned i=0;i<dimension;++i) x[i] = min[i] + dx[i]*tindices[i];
}

unsigned GridVessel::getLocationOnGrid( const std::vector<double>& x, std::vector<double>& dd ){
  plumed_dbg_assert( bounds_set && x.size()==dimension && dd.size()==dimension );
  getIndices( bold, tmp_indices ); bool changebox=false;
  for(unsigned i=0;i<dimension;++i){
     double bb = x[i] - min[i];
     if ( bb<0.0 || bb>dx[i]*nbin[i] ){
        getAction()->error("Extrapolation of function is not allowed");
     } else if( bb<tmp_indices[i]*dx[i] || bb>(tmp_indices[i]+1)*dx[i] ){
        tmp_indices[i]=static_cast<unsigned>( std::floor(bb/dx[i]) );
        changebox=true;
     }
     dd[i] = bb/dx[i] - static_cast<double>(tmp_indices[i]);
  }
  if(changebox) bold = getIndex( tmp_indices );
  return bold;
}

void GridVessel::getSplineNeighbors( const unsigned& mybox, std::vector<unsigned>& mysneigh ){
  if( mysneigh.size()!=current_neigh.size() ) mysneigh.resize( current_neigh.size() );   

  if( bold!=mybox ){
     std::vector<unsigned> my_indices( dimension );
     getIndices( mybox, my_indices );
     for(unsigned i=0;i<current_neigh.size();++i){
        unsigned tmp=i;
        for(unsigned j=0;j<dimension;++j){
           unsigned i0=tmp%2+my_indices[j]; tmp/=2;
           if(!pbc[j] && i0==nbin[j]) getAction()->error("Extrapolating function on grid");
           if( pbc[j] && i0==nbin[j]) i0=0;
           tmp_indices[j]=i0;
        }
        current_neigh[i]=getIndex( tmp_indices );
     }
     bold=mybox;
  }
  for(unsigned i=0;i<current_neigh.size();++i) mysneigh[i]=current_neigh[i];
}

double GridVessel::getGridElement( const unsigned& ipoint, const unsigned& jelement ) const {
  plumed_dbg_assert( bounds_set && ipoint<npoints && jelement<nper );
  return data[ nper*ipoint + jelement ];
}

void GridVessel::setGridElement( const unsigned& ipoint, const unsigned& jelement, const double& value ){
  plumed_dbg_assert( bounds_set && ipoint<npoints && jelement<nper );
  data[ nper*ipoint + jelement ] = value;
}

void GridVessel::addToGridElement( const unsigned& ipoint, const unsigned& jelement, const double& value ){
  plumed_dbg_assert( bounds_set && ipoint<npoints && jelement<nper );
  data[ nper*ipoint + jelement ] += value;
}

double GridVessel::getGridElement( const std::vector<unsigned>& indices, const unsigned& jelement ) const {
  return getGridElement( getIndex( indices ), jelement );
}

void GridVessel::setGridElement( const std::vector<unsigned>& indices, const unsigned& jelement, const double& value ){
  setGridElement( getIndex( indices ), jelement, value );
}

void GridVessel::addToGridElement( const std::vector<unsigned>& indices, const unsigned& jelement, const double& value ){
  addToGridElement( getIndex( indices ), jelement, value );
}

std::vector<std::string> GridVessel::getMin() const {
  plumed_dbg_assert( bounds_set ); return str_min;
}
  
std::vector<std::string> GridVessel::getMax() const {
  plumed_dbg_assert( bounds_set ); return str_max;
}

std::vector<unsigned> GridVessel::getNbin() const {
  plumed_dbg_assert( bounds_set );
  std::vector<unsigned> ngrid( dimension );
  for(unsigned i=0;i<dimension;++i){
      if( !pbc[i] ) ngrid[i]=nbin[i] - 1;
      else ngrid[i]=nbin[i];
  }
  return ngrid;
}

void GridVessel::getNeighbors( const std::vector<double>& pp, const std::vector<unsigned>& nneigh, std::vector<unsigned>& neighbors ) const {
  plumed_dbg_assert( bounds_set && nneigh.size()==dimension );

  std::vector<unsigned> indices( dimension );  
  for(unsigned i=0;i<dimension;++i) indices[i] = std::floor( (pp[i]-min[i])/dx[i] );

  unsigned num_neigh=1; std::vector<unsigned> small_bin( dimension );
  for(unsigned i=0;i<dimension;++i){
     small_bin[i]=(2*nneigh[i]+1);
     num_neigh *=small_bin[i];
  }

  neighbors.resize( num_neigh );
  std::vector<unsigned> s_indices(dimension), t_indices(dimension);
  for(unsigned index=0;index<num_neigh;++index){
      convertIndexToIndices( index, small_bin, s_indices );
      for(unsigned i=0;i<dimension;++i){
          int i0=s_indices[i]-nneigh[i]+indices[i];
          if(!pbc[i] && i0<0)        plumed_merror("grid is not large enough");
          if(!pbc[i] && i0>=nbin[i]) plumed_merror("grid is not large engouh");
          if( pbc[i] && i0<0)        i0=nbin[i]-(-i0)%nbin[i];
          if( pbc[i] && i0>=nbin[i]) i0%=nbin[i];
          t_indices[i]=static_cast<unsigned>(i0);
      }
      neighbors[index]=getIndex( t_indices );
  }
}

void GridVessel::clear(){
  if( !nomemory ) return ;
  data.assign( data.size(), 0.0 );
}

void GridVessel::setCubeUnits( const double& units ){
  cube_units=units;
}

double GridVessel::getCubeUnits() const {
  return cube_units;
}

}
}
