/* 
	================================================================================
	Copyright (c) 2010, Jose Esteve. http://www.joesfer.com
	All rights reserved. 

	Redistribution and use in source and binary forms, with or without modification, 
	are permitted provided that the following conditions are met: 

	* Redistributions of source code must retain the above copyright notice, this 
	  list of conditions and the following disclaimer. 
	
	* Redistributions in binary form must reproduce the above copyright notice, 
	  this list of conditions and the following disclaimer in the documentation 
	  and/or other materials provided with the distribution. 
	
	* Neither the name of the organization nor the names of its contributors may 
	  be used to endorse or promote products derived from this software without 
	  specific prior written permission. 

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
	ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR 
	ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
	ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
	================================================================================
*/

#include "GrowerNode.h"
#include "GrowerData.h"
#include "NearestNeighbors.h"

#include <maya/MPlug.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MVectorArray.h>
#include <maya/MFnPointArrayData.h>
#include <maya/MIntArray.h>
#include <maya/MPointArray.h>
#include <maya/MVector.h>
#include <maya/MFnPluginData.h>
#include <maya/MGlobal.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnVectorArrayData.h>
#include <maya/MFnMatrixData.h>

#include <stack>

//////////////////////////////////////////////////////////////////////
//
// Error checking
//
//    MCHECKERROR       - check the status and print the given error message
//    MCHECKERRORNORET  - same as above but does not return
//
//////////////////////////////////////////////////////////////////////

#define MCHECKERROR(STAT,MSG)       \
	if ( MS::kSuccess != STAT ) {   \
	cerr << MSG << endl;        \
	return MS::kFailure;    \
	}

#define MCHECKERRORNORET(STAT,MSG)  \
	if ( MS::kSuccess != STAT ) {   \
	cerr << MSG << endl;        \
	}

// You MUST change this to a unique value!!!  The typeId is a 32bit value used
// to identify this type of node in the binary file format.  
//
const MTypeId   Grower::id( 0x80097 );
const MString	Grower::typeName( "GrowerNode" );

// Attributes
MObject		Grower::inputSamples;
MObject		Grower::inputPoints;
MObject		Grower::inputNormals;
MObject		Grower::inputPosition;
MObject		Grower::world2Local;
MObject		Grower::searchRadius;
MObject		Grower::killRadius;
MObject		Grower::growDist;
MObject		Grower::maxNeighbors;
MObject		Grower::aoMeshData;


MStatus Grower::compute( const MPlug& plug, MDataBlock& data )
//
//	Description:
//		This method computes the value of the given output plug based
//		on the values of the input attributes.
//
//	Arguments:
//		plug - the plug to compute
//		data - object that provides access to the attributes for this node
//
{
	MStatus stat;
	if ( plug == aoMeshData ) {

		MDataHandle inputPointsHandle = data.inputValue( inputSamples, &stat );

		MObject pointArrayObj = inputPointsHandle.child( inputPoints ).data();
		MFnPointArrayData pointVecData;
		pointVecData.setObject(pointArrayObj);
		MPointArray pointVec = pointVecData.array();

		MObject normalArrayObj = inputPointsHandle.child( inputNormals ).data();
		MFnVectorArrayData normalVecData;
		normalVecData.setObject(normalArrayObj);
		MVectorArray normalVec = normalVecData.array();

		MFnPluginData fnDataCreator;
		MTypeId tmpid( GrowerData::id );
		GrowerData * newData = NULL;

		MDataHandle outHandle = data.outputValue( aoMeshData );	
		newData = (GrowerData*)outHandle.asPluginData();

		if ( newData == NULL ) {
			// Create some output data
			fnDataCreator.create( tmpid, &stat );
			MCHECKERROR( stat, "compute : error creating GrowerData")
			newData = (GrowerData*)fnDataCreator.data( &stat );
			MCHECKERROR( stat, "compute : error gettin at proxy GrowerData object")
		}

		// compute the output values			

		MBoundingBox srcBounds;
		srcBounds.clear();
		for( unsigned int i = 0; i < pointVec.length(); i++ ) {
			srcBounds.expand( pointVec[ i ] );
		}		

		MPoint sourcePos = data.inputValue( Grower::inputPosition ).asFloatVector();
		MFnMatrixData matrixData( data.inputValue( Grower::world2Local ).data() );
		MMatrix matrix = matrixData.matrix();
		sourcePos *= matrix;
		float maxExtents = (float)__max( srcBounds.width(), __max( srcBounds.height(), srcBounds.depth() ) );
		float searchRadius = data.inputValue( Grower::searchRadius ).asFloat() * maxExtents;
		float killRadius = data.inputValue( Grower::killRadius ).asFloat() * maxExtents;
		float nodeGrowDist = data.inputValue( Grower::growDist ).asFloat() * maxExtents;
		int maxNeighbors = data.inputValue( Grower::maxNeighbors ).asInt();

		newData->nodes.resize( 0 );
#if GROWER_DISPLAY_DEBUG_INFO
		newData->samples.resize( 0 );
		Grow( pointVec, normalVec, sourcePos, searchRadius, killRadius, maxNeighbors, nodeGrowDist, newData->nodes, newData->samples );
#else 
		Grow( pointVec, normalVec, sourcePos, searchRadius, killRadius, maxNeighbors, nodeGrowDist, newData->nodes );
#endif
		newData->bounds.clear();
		for( unsigned int i = 0; i < newData->nodes.size(); i++ ) {
			newData->bounds.expand( newData->nodes[ i ].pos );
		}


		// Assign the new data to the outputSurface handle

		if ( newData != outHandle.asPluginData() ) {
			outHandle.set( newData );
		}

		data.setClean(plug);
		return MS::kSuccess;

	}

	return MS::kInvalidParameter;
}


void* Grower::creator()
//
//	Description:
//		this method exists to give Maya a way to create new objects
//      of this type. 
//
//	Return Value:
//		a new object of this type
//
{
	return new Grower();
}

MStatus Grower::initialize()
//
//	Description:
//		This method is called to create and initialize all of the attributes
//      and attribute dependencies for this node type.  This is only called 
//		once when the node type is registered with Maya.
//
//	Return Values:
//		MS::kSuccess
//		MS::kFailure
//		
{
	// This sample creates a single input float attribute and a single
	// output float attribute.
	//
	MFnNumericAttribute nFn;
	MFnTypedAttribute	typedFn;	
	MFnCompoundAttribute cFn;
	MStatus				stat;

	inputPoints = typedFn.create( "samplesPoints", "sp", MFnData::kPointArray );
	typedFn.setStorable( false );
	typedFn.setWritable( true );

	inputNormals = typedFn.create( "samplesNormals", "sn", MFnData::kVectorArray );
	typedFn.setStorable( false );
	typedFn.setWritable( true );

	inputSamples = cFn.create( "samples", "s" );
	cFn.setWritable( true );
	cFn.addChild( inputPoints );
	cFn.addChild( inputNormals );
	cFn.setHidden( true );

	inputPosition = nFn.createPoint( "inputPos", "ip" );
	nFn.setStorable( false );
	nFn.setWritable( true );

	world2Local	= typedFn.create( "worldToLocal", "wtl", MFnData::kMatrix );
	typedFn.setStorable( false );
	typedFn.setWritable( true );

	searchRadius = nFn.create( "searchRadius", "sr", MFnNumericData::kFloat, 0.5f );
	nFn.setSoftMin( 0.001f );
	nFn.setSoftMax( 1.0f );
	nFn.setStorable( true );
	nFn.setWritable( true );

	killRadius = nFn.create( "killRadius", "kr", MFnNumericData::kFloat, 0.01f );
	nFn.setSoftMin( 0.001f );
	nFn.setSoftMax( 0.1f );
	nFn.setStorable( true );
	nFn.setWritable( true );

	growDist = nFn.create( "growDist", "gd", MFnNumericData::kFloat, 0.01f );
	nFn.setSoftMin( 0.0005f );
	nFn.setSoftMax( 0.01f );
	nFn.setStorable( true );
	nFn.setWritable( true );

	maxNeighbors = nFn.create( "maxNeighbors", "mnb", MFnNumericData::kInt, 10 );
	nFn.setSoftMin( 1 );
	nFn.setSoftMax( 1000 );
	nFn.setStorable( true );
	nFn.setWritable( true );

	aoMeshData = typedFn.create( "output", "out", GrowerData::id );
	typedFn.setWritable( false );
	typedFn.setStorable(false);
	typedFn.setHidden( true );

	// Add the attributes we have created to the node
	//
	stat = addAttribute( inputSamples );
	if (!stat) { stat.perror("addAttribute"); return stat;}
	stat = addAttribute( inputPosition );
	if (!stat) { stat.perror("addAttribute"); return stat;}
	stat = addAttribute( world2Local );
	if (!stat) { stat.perror("addAttribute"); return stat;}
	stat = addAttribute( aoMeshData );
	if (!stat) { stat.perror("addAttribute"); return stat;}
	stat = addAttribute( searchRadius );
	if (!stat) { stat.perror("addAttribute"); return stat;}
	stat = addAttribute( killRadius );
	if (!stat) { stat.perror("addAttribute"); return stat;}
	stat = addAttribute( growDist );
	if (!stat) { stat.perror("addAttribute"); return stat;}
	stat = addAttribute( maxNeighbors );
	if (!stat) { stat.perror("addAttribute"); return stat;}

	attributeAffects( inputSamples, aoMeshData );
	attributeAffects( inputPosition, aoMeshData );
	attributeAffects( world2Local, aoMeshData );
	attributeAffects( searchRadius, aoMeshData );
	attributeAffects( killRadius, aoMeshData );
	attributeAffects( growDist, aoMeshData );
	attributeAffects( maxNeighbors, aoMeshData );

	return MS::kSuccess;

}

//////////////////////////////////////////////////////////////////////////
#if GROWER_DISPLAY_DEBUG_INFO
void Grower::Grow( const MPointArray& points, const MVectorArray& normals, const MPoint& sourcePos, const float searchRadius, const float killRadius, const int maxNeighbors, const float nodeGrowDist, std::vector< growerNode_t >& nodes, std::vector< attractionPointVis_t >& attractors ) {
#else
void Grower::Grow( const MPointArray& points, const MVectorArray& normals, const MPoint& sourcePos, const float searchRadius, const float killRadius, const int maxNeighbors, const float nodeGrowDist, std::vector< growerNode_t >& nodes ) {
#endif
	KdTree knn;
	if ( !knn.Init( points, normals ) ) {
		return;
	}

	std::vector< size_t > aliveNodes;

	growerNode_t seed;
	seed.pos = sourcePos;

	AttractionPoint** neighbors = (AttractionPoint**)alloca( ( maxNeighbors + 1 ) * sizeof(AttractionPoint*) );

	nodes.push_back( seed );
	aliveNodes.push_back( 0 );

	std::vector< AttractionPoint* > affectedPoints;

	while( !aliveNodes.empty() ) {
		vector< size_t > newNodes;
		{
			affectedPoints.resize( 0 );

			// find the closest attraction point to each alive node

			for( size_t i = 0; i < aliveNodes.size(); i++ ) {
				const size_t aliveNode = aliveNodes[ i ];

				size_t found = knn.NearestNeighbors( nodes[ aliveNode ].pos, searchRadius, maxNeighbors, neighbors );
				assert( found <= maxNeighbors );

				for( size_t j = 0; j < found; j++ ) {
					AttractionPoint* neighbor = neighbors[ j ];

					assert( neighbor->active );

					bool found = false;
					for( size_t k = 0; k < affectedPoints.size(); k++ ) {
						if ( affectedPoints[ k ] == neighbor ) {
							found = true;
							break;
						}
					}
					if ( !found ) {
						affectedPoints.push_back( neighbor );
					}


					if ( neighbor->closestNode != UINT_MAX ) {
						if( neighbor->closestNode != aliveNode ) {
							float dist = (float)nodes[ aliveNode ].pos.distanceTo( neighbor->pos );
							if ( dist < neighbors[ j ]->dist ) {
								neighbor->closestNode = aliveNode;
								neighbor->dist = dist;
							}
						}
					} else {
						neighbor->closestNode = aliveNode;													
						neighbor->dist		= (float)nodes[ aliveNode ].pos.distanceTo( neighbor->pos );
					}
				}
			}

			// those nodes which are marked as closest to an attraction point
			// are the candidates to spawn new nodes, and therefore are the
			// only ones which remain active for the next iteration
			aliveNodes.resize(0);
			for( size_t i = 0; i < affectedPoints.size(); i++ ) {
				bool found = false;
				size_t node = affectedPoints[ i ]->closestNode;

				for( size_t j = 0; j < aliveNodes.size(); j++ ) {
					if( aliveNodes[ j ] == node ) {
						found = true;
						break;
					}
				}
				if (!found) {
					aliveNodes.push_back( node );
				}
			}

			// spawn new nodes	
			for( int i = 0; i < aliveNodes.size(); i++ ) {

				const size_t nodeIdx = aliveNodes[ i ];
				growerNode_t& srcNode = nodes[ nodeIdx ];

				MVector growDirection( 0, 0, 0 );
				size_t nAttractors = 0;

				for( size_t j = 0; j < affectedPoints.size(); j++ ) {

					if ( affectedPoints[ j ]->closestNode != nodeIdx ) continue;

					nAttractors ++;
					MVector dir = affectedPoints[ j ]->pos - srcNode.pos;
					dir.normalize();
					growDirection += dir;
				}

				assert( nAttractors > 0 );
				growDirection.normalize();

				growerNode_t newNode;
				newNode.pos = srcNode.pos + nodeGrowDist * growDirection;

				bool duplicated = false;
				for( size_t j = 0; j < srcNode.children.size(); j++ ) {
					if( nodes[ srcNode.children[ j ] ].pos.distanceTo( newNode.pos ) <= 0.0001f ) {
						duplicated = true;
						break;
					}
				}

				if ( duplicated ) {
					// erase active element, as it is stuck in a loop trying to produce the same children
					aliveNodes[ i ] = aliveNodes[ aliveNodes.size() - 1 ];
					aliveNodes.resize( aliveNodes.size() - 1 );
					i--;
				} else {
					newNode.parent = nodeIdx;
					size_t newNodeIdx = nodes.size();
					srcNode.children.push_back( newNodeIdx );
					nodes.push_back( newNode );
					newNodes.push_back( newNodeIdx );
				}
			}
			for( size_t i = 0; i < newNodes.size(); i++ ) { 
				aliveNodes.push_back( newNodes[ i ] );
			}
		}

		// use the new spawned nodes to kill close attractor points
		for( size_t i = 0; i < newNodes.size(); i++ ) {
			size_t found = knn.NearestNeighbors( nodes[ newNodes[ i ] ].pos, killRadius, maxNeighbors, neighbors );
			assert( found <= maxNeighbors );
			for( size_t j = 0; j < found; j++ ) {
				neighbors[ j ]->active = false;
			}
		}
	}

	// reactivate all the samples, we're going to retrieve the normals from them
	AttractionPoint* attractors = knn.pm->GetSamples();
	for( unsigned int i = 0; i < knn.pm->NumSamples(); i++ ) {
		attractors[ i ].active = true;
	}

	const MVector zero(0,0,0);
	const double minCosAngle = cos( 3.14159265 / 4 ); // 45 degrees
	size_t numNodes = nodes.size(); // size will change inside the loop
	for( size_t i = 0; i < numNodes; i++ ) {
		growerNode_t& node = nodes[ i ];
		// set normals
		size_t found = knn.NearestNeighbors( node.pos, killRadius, 1, neighbors );
		if ( found == 1 ) {
			node.surfaceNormal = neighbors[0]->normal;
		} else if ( node.parent != INVALID_PARENT && !nodes[ node.parent ].surfaceNormal.isEquivalent( zero, 0.001f ) ) {
			node.surfaceNormal = nodes[ node.parent ].surfaceNormal;
		/*} else if ( node.children.size() > 0 ) {
			MVector avgNormal;
			for( size_t j = 0; j < node.children.size(); j++ ) {
				avgNormal += nodes[ node.children[ j ] ].surfaceNormal;
			}
			node.surfaceNormal = avgNormal / (double)node.children.size();
		} else {*/
		} else {
			node.surfaceNormal = MVector( 0, 1, 0 );
		}

		//
		if ( node.parent != INVALID_PARENT ) {
			growerNode_t& parent = nodes[ node.parent ];
			MVector fromParent = node.pos - parent.pos;
			const double fromParentLength = fromParent.length();
			fromParent /= fromParentLength;
			for( size_t j = 0; j < node.children.size(); j++ ) {
				growerNode_t& child = nodes[ node.children[ j ] ];
				MVector toChild = child.pos - node.pos;
				const double toChildLength = toChild.length();
				toChild /= toChildLength;
				const double cosAngle = fromParent * toChild;
				if (  cosAngle < minCosAngle ) { 

					child.parent = node.parent;
					parent.children.push_back( node.children[ j ] );
					if ( node.children.size() > 1 ) {
						node.children[ j ] = node.children.back();
						node.children.resize( node.children.size() - 1 );
						j--;
					} else {
						node.children.clear();
						break;
					}							
				}
			}
		}
	}

#if GROWER_DISPLAY_DEBUG_INFO
	const AttractionPoint* samples = knn.pm->GetSamples();
	unsigned int nSamples = knn.pm->NumSamples();
	for( unsigned int i = 0; i < nSamples; i++ ) {
		attractionPointVis_t p;
		p.pos = samples[ i ].pos;
		p.active = samples[ i ].active;
		attractors.push_back( p );
	}
#endif
}