// ---------------------------------------------------------------------
//
// Copyright (c) 2017 The Regents of the University of Michigan and DFT-FE authors.
//
// This file is part of the DFT-FE code.
//
// The DFT-FE code is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE at
// the top level of the DFT-FE distribution.
//
// ---------------------------------------------------------------------
//
// @author Sambit Das (2017)
//
#ifdef ENABLE_PERIODIC_BC 
//compute configurational stress contribution from all terms except the nuclear self energy
template<unsigned int FEOrder>
void forceClass<FEOrder>::computeStressEEshelbyEPSPEnlEk()
{
  const unsigned int numberGlobalAtoms = dftPtr->atomLocations.size();
  const unsigned int numberImageCharges = dftPtr->d_imageIds.size();
  const unsigned int totalNumberAtoms = numberGlobalAtoms + numberImageCharges; 
  const bool isPseudopotential = dftParameters::isPseudopotential;
  const unsigned int numVectorizedArrayElements=VectorizedArray<double>::n_array_elements;
  const MatrixFree<3,double> & matrix_free_data=dftPtr->matrix_free_data;

  FEEvaluation<C_DIM,1,C_num1DQuad<FEOrder>(),C_DIM>  forceEval(matrix_free_data,d_forceDofHandlerIndex, 0);
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> phiTotEval(matrix_free_data,dftPtr->phiTotDofHandlerIndex, 0);
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> phiTotInEval(matrix_free_data,dftPtr->phiTotDofHandlerIndex, 0);  
  FEEvaluation<C_DIM,FEOrder,C_num1DQuad<FEOrder>(),1> phiExtEval(matrix_free_data, dftPtr->phiExtDofHandlerIndex, 0);
  QGauss<C_DIM>  quadrature(C_num1DQuad<FEOrder>());   
  FEValues<C_DIM> feVselfValues (dftPtr->FE, quadrature, update_gradients | update_quadrature_points);
  FEValues<C_DIM> psiValues(dftPtr->FEEigen, quadrature, update_values | update_gradients| update_hessians);

  const unsigned int numQuadPoints=forceEval.n_q_points;
  const unsigned int numEigenVectors=dftPtr->eigenVectorsOrig[0].size();  
  const unsigned int numKPoints=dftPtr->d_kPointWeights.size();

  DoFHandler<C_DIM>::active_cell_iterator subCellPtr;
  Tensor<1,2,VectorizedArray<double> > zeroTensor1;zeroTensor1[0]=make_vectorized_array(0.0);zeroTensor1[1]=make_vectorized_array(0.0);
  Tensor<1,2, Tensor<1,C_DIM,VectorizedArray<double> > > zeroTensor2;
  Tensor<1,C_DIM,VectorizedArray<double> > zeroTensor3;
  Tensor<2,C_DIM,VectorizedArray<double> > zeroTensor4;
  Tensor<1,2, Tensor<2,C_DIM,VectorizedArray<double> > > zeroTensor5;  
  for (unsigned int idim=0; idim<C_DIM; idim++){
    zeroTensor2[0][idim]=make_vectorized_array(0.0);
    zeroTensor2[1][idim]=make_vectorized_array(0.0);
    zeroTensor3[idim]=make_vectorized_array(0.0);
  }
  for (unsigned int idim=0; idim<C_DIM; idim++)
  {
    for (unsigned int jdim=0; jdim<C_DIM; jdim++)
    {
	zeroTensor4[idim][jdim]=make_vectorized_array(0.0);
    }
  }
  zeroTensor5[0]=zeroTensor4;
  zeroTensor5[1]=zeroTensor4;

  VectorizedArray<double> phiExtFactor=make_vectorized_array(0.0);
  std::vector<std::vector<double> > projectorKetTimesPsiTimesVReal;
  std::vector<std::vector<std::vector<std::complex<double> > > > projectorKetTimesPsiTimesVComplexKPoints(numKPoints);
  if (isPseudopotential){
    phiExtFactor=make_vectorized_array(1.0);
    for (unsigned int ikPoint=0; ikPoint<numKPoints; ++ikPoint){
         computeNonLocalProjectorKetTimesPsiTimesV(dftPtr->eigenVectorsOrig[ikPoint],
			                           projectorKetTimesPsiTimesVReal,
                                                   projectorKetTimesPsiTimesVComplexKPoints[ikPoint],
						   ikPoint);	
    }    
  }

  std::vector<VectorizedArray<double> > rhoQuads(numQuadPoints,make_vectorized_array(0.0));
  std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradRhoQuads(numQuadPoints,zeroTensor3);
  std::vector<Tensor<2,C_DIM,VectorizedArray<double> > > hessianRhoQuads(numQuadPoints,zeroTensor4);
  std::vector<VectorizedArray<double> > excQuads(numQuadPoints,make_vectorized_array(0.0));
  std::vector<VectorizedArray<double> > pseudoVLocQuads(numQuadPoints,make_vectorized_array(0.0));
  std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > gradPseudoVLocQuads(numQuadPoints,zeroTensor3);
  std::vector<VectorizedArray<double> > vEffRhoInQuads(numQuadPoints,make_vectorized_array(0.0));
  std::vector<VectorizedArray<double> > vEffRhoOutQuads(numQuadPoints,make_vectorized_array(0.0)); 
  std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > derExchCorrEnergyWithGradRhoInQuads(numQuadPoints,zeroTensor3);
  std::vector<Tensor<1,C_DIM,VectorizedArray<double> > > derExchCorrEnergyWithGradRhoOutQuads(numQuadPoints,zeroTensor3);

  for (unsigned int cell=0; cell<matrix_free_data.n_macro_cells(); ++cell)
  {
    forceEval.reinit(cell);

    phiTotEval.reinit(cell);
    phiTotEval.read_dof_values_plain(dftPtr->poissonPtr->phiTotRhoOut);//read without taking constraints into account
    phiTotEval.evaluate(true,true);

    phiTotInEval.reinit(cell);
    phiTotInEval.read_dof_values_plain(dftPtr->poissonPtr->phiTotRhoIn);//read without taking constraints into account
    phiTotInEval.evaluate(true,true);    

    phiExtEval.reinit(cell);
    phiExtEval.read_dof_values_plain(dftPtr->poissonPtr->phiExt);
    phiExtEval.evaluate(true,true);

    std::fill(rhoQuads.begin(),rhoQuads.end(),make_vectorized_array(0.0));
    std::fill(gradRhoQuads.begin(),gradRhoQuads.end(),zeroTensor3);
    std::fill(hessianRhoQuads.begin(),hessianRhoQuads.end(),zeroTensor4);
    std::fill(excQuads.begin(),excQuads.end(),make_vectorized_array(0.0));
    std::fill(pseudoVLocQuads.begin(),pseudoVLocQuads.end(),make_vectorized_array(0.0));
    std::fill(gradPseudoVLocQuads.begin(),gradPseudoVLocQuads.end(),zeroTensor3);
    std::fill(vEffRhoInQuads.begin(),vEffRhoInQuads.end(),make_vectorized_array(0.0));
    std::fill(vEffRhoOutQuads.begin(),vEffRhoOutQuads.end(),make_vectorized_array(0.0));
    std::fill(derExchCorrEnergyWithGradRhoInQuads.begin(),derExchCorrEnergyWithGradRhoInQuads.end(),zeroTensor3);
    std::fill(derExchCorrEnergyWithGradRhoOutQuads.begin(),derExchCorrEnergyWithGradRhoOutQuads.end(),zeroTensor3);
    for (unsigned int q=0; q<numQuadPoints; ++q)
    {
	 vEffRhoInQuads[q]=phiTotInEval.get_value(q); 
	 vEffRhoOutQuads[q]=phiTotEval.get_value(q); 
    }     
    //allocate storage for vector of quadPoints, nonlocal atom id, pseudo wave, k point
    //FIXME: flatten nonlocal atomid id and pseudo wave and k point
    std::vector<std::vector<std::vector<std::vector<Tensor<1,2,VectorizedArray<double> > > > > >ZetaDeltaVQuads;
    std::vector<std::vector<std::vector<std::vector<Tensor<1,2, Tensor<2,C_DIM,VectorizedArray<double> > > > > > >gradZetalmDeltaVlDyadicDistImageAtomsQuads; 
    if(isPseudopotential)
    {
        ZetaDeltaVQuads.resize(numQuadPoints);	
	gradZetalmDeltaVlDyadicDistImageAtomsQuads.resize(numQuadPoints);
	for (unsigned int q=0; q<numQuadPoints; ++q)
	{
	  ZetaDeltaVQuads[q].resize(d_nonLocalPSP_ZetalmDeltaVl.size());	    
	  gradZetalmDeltaVlDyadicDistImageAtomsQuads[q].resize(d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint.size());
	  for (unsigned int i=0; i < d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint.size(); ++i)
	  {
	    const int numberPseudoWaveFunctions = d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint[i].size();
	    ZetaDeltaVQuads[q][i].resize(numberPseudoWaveFunctions);
	    gradZetalmDeltaVlDyadicDistImageAtomsQuads[q][i].resize(numberPseudoWaveFunctions);
	    for (unsigned int iPseudoWave=0; iPseudoWave < numberPseudoWaveFunctions; ++iPseudoWave)
	    {
		ZetaDeltaVQuads[q][i][iPseudoWave].resize(numKPoints,zeroTensor1);
		gradZetalmDeltaVlDyadicDistImageAtomsQuads[q][i][iPseudoWave].resize(numKPoints,zeroTensor5);
	    }
	  }
	}
    }

    const unsigned int numSubCells=matrix_free_data.n_components_filled(cell);
    //For LDA
    std::vector<double> exchValRhoOut(numQuadPoints);
    std::vector<double> corrValRhoOut(numQuadPoints);
    std::vector<double> exchPotValRhoOut(numQuadPoints);
    std::vector<double> corrPotValRhoOut(numQuadPoints);
    std::vector<double> exchValRhoIn(numQuadPoints);
    std::vector<double> corrValRhoIn(numQuadPoints);
    std::vector<double> exchPotValRhoIn(numQuadPoints);
    std::vector<double> corrPotValRhoIn(numQuadPoints);
    //
    //For GGA
    std::vector<double> sigmaValRhoOut(numQuadPoints);
    std::vector<double> derExchEnergyWithDensityValRhoOut(numQuadPoints), derCorrEnergyWithDensityValRhoOut(numQuadPoints), derExchEnergyWithSigmaRhoOut(numQuadPoints),derCorrEnergyWithSigmaRhoOut(numQuadPoints);
    std::vector<double> sigmaValRhoIn(numQuadPoints);
    std::vector<double> derExchEnergyWithDensityValRhoIn(numQuadPoints), derCorrEnergyWithDensityValRhoIn(numQuadPoints), derExchEnergyWithSigmaRhoIn(numQuadPoints),derCorrEnergyWithSigmaRhoIn(numQuadPoints);
    std::vector<Tensor<1,C_DIM,double > > gradRhoIn(numQuadPoints);
    std::vector<Tensor<1,C_DIM,double > > gradRhoOut(numQuadPoints);
    //
    for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell)
    {
       subCellPtr= matrix_free_data.get_cell_iterator(cell,iSubCell);
       dealii::CellId subCellId=subCellPtr->id();
       if(dftParameters::xc_id == 4)
       {
	  for (unsigned int q = 0; q < numQuadPoints; ++q)
	  {
	      gradRhoOut[q][0] = ((*dftPtr->gradRhoOutValues)[subCellId][3*q + 0]);
	      gradRhoOut[q][1] = ((*dftPtr->gradRhoOutValues)[subCellId][3*q + 1]);
	      gradRhoOut[q][2] = ((*dftPtr->gradRhoOutValues)[subCellId][3*q + 2]);
	      sigmaValRhoOut[q] = gradRhoOut[q].norm_square();
	     
	      gradRhoIn[q][0] = ((*dftPtr->gradRhoInValues)[subCellId][3*q + 0]);
	      gradRhoIn[q][1] = ((*dftPtr->gradRhoInValues)[subCellId][3*q + 1]);
	      gradRhoIn[q][2] = ((*dftPtr->gradRhoInValues)[subCellId][3*q + 2]);
	      sigmaValRhoIn[q] = gradRhoIn[q].norm_square();	      
	  }	   
	  xc_gga_exc_vxc(&(dftPtr->funcX),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&sigmaValRhoOut[0],&exchValRhoOut[0],&derExchEnergyWithDensityValRhoOut[0],&derExchEnergyWithSigmaRhoOut[0]);
	  xc_gga_exc_vxc(&(dftPtr->funcC),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&sigmaValRhoOut[0],&corrValRhoOut[0],&derCorrEnergyWithDensityValRhoOut[0],&derCorrEnergyWithSigmaRhoOut[0]);
	  xc_gga_exc_vxc(&(dftPtr->funcX),numQuadPoints,&((*dftPtr->rhoInValues)[subCellId][0]),&sigmaValRhoIn[0],&exchValRhoIn[0],&derExchEnergyWithDensityValRhoIn[0],&derExchEnergyWithSigmaRhoIn[0]);
	  xc_gga_exc_vxc(&(dftPtr->funcC),numQuadPoints,&((*dftPtr->rhoInValues)[subCellId][0]),&sigmaValRhoIn[0],&corrValRhoIn[0],&derCorrEnergyWithDensityValRhoIn[0],&derCorrEnergyWithSigmaRhoIn[0]);
          for (unsigned int q=0; q<numQuadPoints; ++q)
	  {
	     excQuads[q][iSubCell]=exchValRhoOut[q]+corrValRhoOut[q];
	     const double temp = derExchEnergyWithSigmaRhoOut[q]+derCorrEnergyWithSigmaRhoOut[q];
	     vEffRhoInQuads[q][iSubCell]+= derExchEnergyWithDensityValRhoIn[q]+derCorrEnergyWithDensityValRhoIn[q];
             vEffRhoOutQuads[q][iSubCell]+= derExchEnergyWithDensityValRhoOut[q]+derCorrEnergyWithDensityValRhoOut[q];
	      for (unsigned int idim=0; idim<C_DIM; idim++)
	      {	     
	         derExchCorrEnergyWithGradRhoInQuads[q][idim][iSubCell]=2.0*(derExchEnergyWithSigmaRhoIn[q]+derCorrEnergyWithSigmaRhoIn[q])*gradRhoIn[q][idim];
	         derExchCorrEnergyWithGradRhoOutQuads[q][idim][iSubCell]=2.0*(derExchEnergyWithSigmaRhoOut[q]+derCorrEnergyWithSigmaRhoOut[q])*gradRhoOut[q][idim];
	      }
          }	  
	  
       }
       else
       {
          xc_lda_exc(&(dftPtr->funcX),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&exchValRhoOut[0]);
          xc_lda_exc(&(dftPtr->funcC),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&corrValRhoOut[0]);
	  xc_lda_vxc(&(dftPtr->funcX),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&exchPotValRhoOut[0]);
	  xc_lda_vxc(&(dftPtr->funcC),numQuadPoints,&((*dftPtr->rhoOutValues)[subCellId][0]),&corrPotValRhoOut[0]);
	  xc_lda_vxc(&(dftPtr->funcX),numQuadPoints,&((*dftPtr->rhoInValues)[subCellId][0]),&exchPotValRhoIn[0]);
	  xc_lda_vxc(&(dftPtr->funcC),numQuadPoints,&((*dftPtr->rhoInValues)[subCellId][0]),&corrPotValRhoIn[0]);
          for (unsigned int q=0; q<numQuadPoints; ++q)
	  {
	     excQuads[q][iSubCell]=exchValRhoOut[q]+corrValRhoOut[q];
	     vEffRhoInQuads[q][iSubCell]+= exchPotValRhoIn[q]+corrPotValRhoIn[q];
             vEffRhoOutQuads[q][iSubCell]+= exchPotValRhoOut[q]+corrPotValRhoOut[q];
          }
       }

       for (unsigned int q=0; q<numQuadPoints; ++q)
       {
         rhoQuads[q][iSubCell]=(*dftPtr->rhoOutValues)[subCellId][q];
       }
    }   
   
    std::vector<Tensor<1,2,VectorizedArray<double> > > psiQuads(numQuadPoints*numEigenVectors*numKPoints,zeroTensor1);
    std::vector<Tensor<1,2,Tensor<1,C_DIM,VectorizedArray<double> > > > gradPsiQuads(numQuadPoints*numEigenVectors*numKPoints,zeroTensor2);
    std::vector<Vector<double> > tempPsi(numQuadPoints);
    std::vector<std::vector<Tensor<1,C_DIM,double > > >  tempGradPsi(numQuadPoints);  
    std::vector<std::vector<Tensor<2,C_DIM,double > > >  tempHessianPsi(numQuadPoints);
    for (unsigned int q=0; q<numQuadPoints; ++q)
    {
	  tempPsi[q].reinit(2);
	  tempGradPsi[q].resize(2);
	  tempHessianPsi[q].resize(2);
    }    
    for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell)
    {   
      subCellPtr= dftPtr->matrix_free_data.get_cell_iterator(cell,iSubCell,dftPtr->eigenDofHandlerIndex);
      psiValues.reinit(subCellPtr);
      for (unsigned int ikPoint=0; ikPoint<numKPoints; ++ikPoint)
      { 
        for (unsigned int iEigenVec=0; iEigenVec<numEigenVectors; ++iEigenVec)
        {
		      
	  psiValues.get_function_values(*((dftPtr->eigenVectorsOrig)[ikPoint][iEigenVec]), tempPsi);
          psiValues.get_function_gradients(*((dftPtr->eigenVectorsOrig)[ikPoint][iEigenVec]), tempGradPsi);
          psiValues.get_function_hessians(*((dftPtr->eigenVectorsOrig)[ikPoint][iEigenVec]), tempHessianPsi);
          for (unsigned int q=0; q<numQuadPoints; ++q)
          {
	     const int id=q*numEigenVectors*numKPoints+numEigenVectors*ikPoint+iEigenVec;
	     for (unsigned int icomp=0;icomp<2;++icomp)
	     {
		 psiQuads[id][icomp][iSubCell]=tempPsi[q][icomp];
		 for (unsigned int idim=0; idim<C_DIM; idim++)
		 {
		     gradPsiQuads[id][icomp][idim][iSubCell]=tempGradPsi[q][icomp][idim];
		 }
	     }
	     
             const double partOcc =dftUtils::getPartialOccupancy(dftPtr->eigenValues[ikPoint][iEigenVec],
		                                                 dftPtr->fermiEnergy,
							         C_kb,
							         dftParameters::TVal); 	     
	     const Tensor<1,C_DIM,double > tempGradRhoContribution=2.0*dftPtr->d_kPointWeights[ikPoint]*partOcc*internalforce::computeGradRhoContribution(tempPsi[q], tempGradPsi[q]);
	     const Tensor<2,C_DIM,double > tempHessianRhoContribution=2.0*dftPtr->d_kPointWeights[ikPoint]*partOcc*internalforce::computeHessianRhoContribution(tempPsi[q], tempGradPsi[q], tempHessianPsi[q]);

	     for (unsigned int idim=0; idim<C_DIM; idim++)
	     {
	       gradRhoQuads[q][idim][iSubCell]+=tempGradRhoContribution[idim];

	       for (unsigned int jdim=0; jdim<C_DIM; jdim++)
	       {		 
	         hessianRhoQuads[q][idim][jdim][iSubCell]+=tempHessianRhoContribution[idim][jdim];
	       }
	     }
          }//quad point loop
        } //eigenvector loop
      }//k point loop
      //accumulate hessian rho quad point contribution from all pools
      for (unsigned int q=0; q<numQuadPoints; ++q)
      {      
	for (unsigned int idim=0; idim<C_DIM; idim++)
	{
	  gradRhoQuads[q][idim][iSubCell]=Utilities::MPI::sum(gradRhoQuads[q][idim][iSubCell],dftPtr->interpoolcomm);
	  for (unsigned int jdim=0; jdim<C_DIM; jdim++)
	  {		 
	    hessianRhoQuads[q][idim][jdim][iSubCell]=Utilities::MPI::sum(hessianRhoQuads[q][idim][jdim][iSubCell],dftPtr->interpoolcomm);
;
	  }
	}    
      }
    }//subcell loop

    if(isPseudopotential)
    {
       for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell)
       {
          subCellPtr= matrix_free_data.get_cell_iterator(cell,iSubCell);
          dealii::CellId subCellId=subCellPtr->id();
	  for (unsigned int q=0; q<numQuadPoints; ++q)
	  {
	     pseudoVLocQuads[q][iSubCell]=((*dftPtr->pseudoValues)[subCellId][q]);
	     gradPseudoVLocQuads[q][0][iSubCell]=d_gradPseudoVLoc[subCellId][C_DIM*q+0];
             gradPseudoVLocQuads[q][1][iSubCell]=d_gradPseudoVLoc[subCellId][C_DIM*q+1];
	     gradPseudoVLocQuads[q][2][iSubCell]=d_gradPseudoVLoc[subCellId][C_DIM*q+2];
	  }
	  
	  for (unsigned int q=0; q<numQuadPoints; ++q)
	  {
            for (unsigned int i=0; i < d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint.size(); ++i)
	    {
	      const int numberPseudoWaveFunctions = d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint[i].size();
	      for (unsigned int iPseudoWave=0; iPseudoWave < numberPseudoWaveFunctions; ++iPseudoWave)
	      {	
		if (d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint[i][iPseudoWave].find(subCellId)!=d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint[i][iPseudoWave].end())
		{
                   for (unsigned int ikPoint=0; ikPoint<numKPoints; ++ikPoint)
		   { 
                      ZetaDeltaVQuads[q][i][iPseudoWave][ikPoint][0][iSubCell]=d_nonLocalPSP_ZetalmDeltaVl[i][iPseudoWave][subCellId][ikPoint*numQuadPoints*2+q*2+0];
                      ZetaDeltaVQuads[q][i][iPseudoWave][ikPoint][1][iSubCell]=d_nonLocalPSP_ZetalmDeltaVl[i][iPseudoWave][subCellId][ikPoint*numQuadPoints*2+q*2+1];		       
		      for (unsigned int idim=0; idim<C_DIM; idim++)
		      {
		        for (unsigned int jdim=0; jdim<C_DIM; jdim++)
		        {			  
                           gradZetalmDeltaVlDyadicDistImageAtomsQuads[q][i][iPseudoWave][ikPoint][0][idim][jdim][iSubCell]=d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint[i][iPseudoWave][subCellId][ikPoint*numQuadPoints*C_DIM*C_DIM*2+q*C_DIM*C_DIM*2+idim*C_DIM*2+jdim*2+0];
                           gradZetalmDeltaVlDyadicDistImageAtomsQuads[q][i][iPseudoWave][ikPoint][1][idim][jdim][iSubCell]=d_nonLocalPSP_gradZetalmDeltaVlDyadicDistImageAtoms_KPoint[i][iPseudoWave][subCellId][ikPoint*numQuadPoints*C_DIM*C_DIM*2+q*C_DIM*C_DIM*2+idim*C_DIM*2+jdim*2+1];
			}
		      }
		   }
		}//non-trivial cellId check
	      }//iPseudoWave loop
	    }//i loop
	  }//q loop
       }//subcell loop   

       addEPSPStressContribution(feVselfValues,
	                         forceEval,
			         cell,
			         rhoQuads);       
    }//is pseudopotential check

    for (unsigned int q=0; q<numQuadPoints; ++q)
    {
       VectorizedArray<double> phiTot_q =phiTotEval.get_value(q);   
       Tensor<1,C_DIM,VectorizedArray<double> > gradPhiTot_q =phiTotEval.get_gradient(q);
       VectorizedArray<double> phiExt_q =phiExtEval.get_value(q)*phiExtFactor;
       Tensor<2,C_DIM,VectorizedArray<double> > E=eshelbyTensor::getELocEshelbyTensorPeriodicNoKPoints
	                                                     (phiTot_q,
			                                      gradPhiTot_q,
						              rhoQuads[q],
						              gradRhoQuads[q],
						              excQuads[q],
						              derExchCorrEnergyWithGradRhoOutQuads[q],
							      pseudoVLocQuads[q],
							      phiExt_q);

       Tensor<2,C_DIM,VectorizedArray<double> > EKPoints=eshelbyTensor::getELocEshelbyTensorPeriodicKPoints
						             (psiQuads.begin()+q*numEigenVectors*numKPoints,
						              gradPsiQuads.begin()+q*numEigenVectors*numKPoints,
							      dftPtr->d_kPointCoordinates,
							      dftPtr->d_kPointWeights,
							      dftPtr->eigenValues,
							      dftPtr->fermiEnergy,
							      dftParameters::TVal);

       EKPoints+=eshelbyTensor::getEKStress
				(psiQuads.begin()+q*numEigenVectors*numKPoints,
				 gradPsiQuads.begin()+q*numEigenVectors*numKPoints,
			         dftPtr->d_kPointCoordinates,
				 dftPtr->d_kPointWeights,
				 dftPtr->eigenValues,
			         dftPtr->fermiEnergy,
				 dftParameters::TVal);       
       
       if(isPseudopotential)
       {
          EKPoints+=eshelbyTensor::getEnlEshelbyTensorPeriodic(ZetaDeltaVQuads[q],
		                                         projectorKetTimesPsiTimesVComplexKPoints,
						         psiQuads.begin()+q*numEigenVectors*numKPoints,
							 dftPtr->d_kPointWeights,
						         dftPtr->eigenValues,
						         dftPtr->fermiEnergy,
						         dftParameters::TVal); 	   

          EKPoints+=eshelbyTensor::getEnlStress(gradZetalmDeltaVlDyadicDistImageAtomsQuads[q],
		                                 projectorKetTimesPsiTimesVComplexKPoints,
						 psiQuads.begin()+q*numEigenVectors*numKPoints,
					         dftPtr->d_kPointWeights,
						 dftPtr->eigenValues,
						 dftPtr->fermiEnergy,
						 dftParameters::TVal);
  
       }//is pseudopotential check

       E=E*forceEval.JxW(q);
       EKPoints=EKPoints*forceEval.JxW(q);       
       for (unsigned int iSubCell=0; iSubCell<numSubCells; ++iSubCell)
       {
	    for (unsigned int idim=0; idim<C_DIM; ++idim)
	    {
		for (unsigned int jdim=0; jdim<C_DIM; ++jdim)
		{
		    d_stress[idim][jdim]+=E[idim][jdim][iSubCell];
		    d_stressKPoints[idim][jdim]+=EKPoints[idim][jdim][iSubCell];
		}
	    }
       }
    }//quad point loop
  }
}
#endif