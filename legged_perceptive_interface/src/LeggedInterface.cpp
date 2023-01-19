//
// Created by qiayuan on 22-12-27.
//

#include "legged_perceptive_interface/LeggedInterface.h"
#include "legged_perceptive_interface/ConvexRegionSelector.h"
#include "legged_perceptive_interface/LeggedPrecomputation.h"
#include "legged_perceptive_interface/synchronized_module/LeggedReferenceManager.h"

#include <ocs2_core/soft_constraint/StateSoftConstraint.h>
#include <ocs2_pinocchio_interface/PinocchioEndEffectorKinematicsCppAd.h>
#include <ocs2_sphere_approximation/PinocchioSphereKinematics.h>

#include <memory>

namespace legged {
void SphereSdfLeggedInterface::setupOptimalControlProblem(const std::string& taskFile, const std::string& urdfFile,
                                                          const std::string& referenceFile, bool verbose) {
  LeggedInterface::setupOptimalControlProblem(taskFile, urdfFile, referenceFile, verbose);

  std::string elevationLayer = "elevation_before_postprocess";
  planarTerrainPtr_ = std::make_unique<convex_plane_decomposition::PlanarTerrain>();
  planarTerrainPtr_->gridMap.setGeometry(grid_map::Length(5.0, 5.0), 0.03);
  planarTerrainPtr_->gridMap.add(elevationLayer, 0);
  sdfPrt_ = std::make_shared<grid_map::SignedDistanceField>(planarTerrainPtr_->gridMap, elevationLayer, -0.1, 0.1);

  scalar_t thighExcess = 0.025;
  scalar_t calfExcess = 0.02;

  std::vector<std::string> collisionLinks = {"base",    "LF_thigh", "RF_thigh", "LH_thigh", "RH_thigh",
                                             "LF_calf", "RF_calf",  "LH_calf",  "RH_calf"};
  const std::vector<scalar_t>& maxExcesses = {0.05,       thighExcess, thighExcess, thighExcess, thighExcess,
                                              calfExcess, calfExcess,  calfExcess,  calfExcess};

  pinocchioSphereInterfacePrt_ = std::make_shared<PinocchioSphereInterface>(*pinocchioInterfacePtr_, collisionLinks, maxExcesses, 0.6);

  CentroidalModelPinocchioMapping pinocchioMapping(centroidalModelInfo_);
  auto sphereKinematicsPtr = std::make_unique<PinocchioSphereKinematics>(*pinocchioSphereInterfacePrt_, pinocchioMapping);

  std::unique_ptr<SphereSdfConstraint> sphereSdfConstraint(
      new SphereSdfConstraint(*sphereKinematicsPtr, *pinocchioInterfacePtr_, pinocchioMapping, sdfPrt_));

  std::unique_ptr<PenaltyBase> penalty(new RelaxedBarrierPenalty(RelaxedBarrierPenalty::Config(0.1, 1e-3)));
  problemPtr_->stateSoftConstraintPtr->add(
      "sdfConstraint", std::unique_ptr<StateCost>(new StateSoftConstraint(std::move(sphereSdfConstraint), std::move(penalty))));
}

void FootPlacementLeggedInterface::setupOptimalControlProblem(const std::string& taskFile, const std::string& urdfFile,
                                                              const std::string& referenceFile, bool verbose) {
  planarTerrainPtr_ = std::make_shared<convex_plane_decomposition::PlanarTerrain>();

  double width{0.8}, height{0.15}, offset{0.3};
  for (int i = -3; i < 6; ++i) {
    convex_plane_decomposition::PlanarRegion plannerRegion;
    plannerRegion.transformPlaneToWorld.setIdentity();
    plannerRegion.transformPlaneToWorld.translation().x() = i * offset;
    plannerRegion.bbox2d = convex_plane_decomposition::CgalBbox2d(-height / 2, -width / 2, +height / 2, width / 2);
    convex_plane_decomposition::CgalPolygonWithHoles2d boundary;
    boundary.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(+height / 2, +width / 2));
    boundary.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(-height / 2, +width / 2));
    boundary.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(-height / 2, -width / 2));
    boundary.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(+height / 2, -width / 2));
    plannerRegion.boundaryWithInset.boundary = boundary;
    convex_plane_decomposition::CgalPolygonWithHoles2d insets;
    insets.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(+height / 2 - 0.01, +width / 2 - 0.01));
    insets.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(-height / 2 + 0.01, +width / 2 - 0.01));
    insets.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(-height / 2 + 0.01, -width / 2 + 0.01));
    insets.outer_boundary().push_back(convex_plane_decomposition::CgalPoint2d(+height / 2 - 0.01, -width / 2 + 0.01));
    plannerRegion.boundaryWithInset.insets.push_back(insets);
    planarTerrainPtr_->planarRegions.push_back(plannerRegion);
  }

  std::string elevationLayer = "elevation_before_postprocess";
  planarTerrainPtr_->gridMap.setGeometry(grid_map::Length(5.0, 5.0), 0.03);
  planarTerrainPtr_->gridMap.add(elevationLayer, 0);
  sdfPrt_ = std::make_shared<grid_map::SignedDistanceField>(planarTerrainPtr_->gridMap, elevationLayer, -0.1, 0.1);

  LeggedInterface::setupOptimalControlProblem(taskFile, urdfFile, referenceFile, verbose);

  for (size_t i = 0; i < centroidalModelInfo_.numThreeDofContacts; i++) {
    const std::string& footName = modelSettings().contactNames3DoF[i];
    std::unique_ptr<EndEffectorKinematics<scalar_t>> eeKinematicsPtr = getEeKinematicsPtr({footName}, footName);

    std::unique_ptr<FootPlacementConstraint> footPlacementConstraint(
        new FootPlacementConstraint(*referenceManagerPtr_, *eeKinematicsPtr, i, numVertices_));
    std::unique_ptr<PenaltyBase> penalty(new RelaxedBarrierPenalty(RelaxedBarrierPenalty::Config(0.1, 1e-3)));
    problemPtr_->stateSoftConstraintPtr->add(footName + "_footPlacement", std::unique_ptr<StateCost>(new StateSoftConstraint(
                                                                              std::move(footPlacementConstraint), std::move(penalty))));
  }
}

void FootPlacementLeggedInterface::setupReferenceManager(const std::string& taskFile, const std::string& /*urdfFile*/,
                                                         const std::string& referenceFile, bool verbose) {
  auto swingTrajectoryPlanner =
      std::make_unique<SwingTrajectoryPlanner>(loadSwingTrajectorySettings(taskFile, "swing_trajectory_config", verbose), 4);

  std::unique_ptr<EndEffectorKinematics<scalar_t>> eeKinematicsPtr = getEeKinematicsPtr({modelSettings_.contactNames3DoF}, "ALL_FOOT");
  auto convexRegionSelector = std::make_unique<ConvexRegionSelector>(centroidalModelInfo_, planarTerrainPtr_, *eeKinematicsPtr);

  referenceManagerPtr_.reset(new LeggedReferenceManager(loadGaitSchedule(referenceFile, verbose), std::move(swingTrajectoryPlanner),
                                                        std::move(convexRegionSelector)));
}

void FootPlacementLeggedInterface::setupPreComputation(const std::string& /*taskFile*/, const std::string& /*urdfFile*/,
                                                       const std::string& /*referenceFile*/, bool /*verbose*/) {
  problemPtr_->preComputationPtr = std::make_unique<LeggedPreComputation>(
      *pinocchioInterfacePtr_, centroidalModelInfo_, *referenceManagerPtr_->getSwingTrajectoryPlanner(), modelSettings(),
      *dynamic_cast<LeggedReferenceManager&>(*referenceManagerPtr_).getConvexRegionSelectorPtr(), numVertices_);
}

}  // namespace legged