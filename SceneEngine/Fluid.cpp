// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS

#include "Fluid.h"
#include "FluidAdvection.h"
#include "../Math/RegularNumberField.h"
#include "../Math/PoissonSolver.h"
#include "../ConsoleRig/Log.h"
#include "../Utility/Meta/ClassAccessorsImpl.h"

extern "C" void dens_step ( int N, float * x, float * x0, float * u, float * v, float diff, float dt );
extern "C" void vel_step ( int N, float * u, float * v, float * u0, float * v0, float visc, float dt );

#pragma warning(disable:4714)
#pragma push_macro("new")
#undef new
#include <Eigen/Dense>
#pragma pop_macro("new")

namespace SceneEngine
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ReferenceFluidSolver2D::Pimpl
    {
    public:
        std::unique_ptr<float[]> _velU;
        std::unique_ptr<float[]> _velV;
        std::unique_ptr<float[]> _density;
        std::unique_ptr<float[]> _prevVelU;
        std::unique_ptr<float[]> _prevVelV;
        std::unique_ptr<float[]> _prevDensity;
        UInt2 _dimensions;
    };

    void ReferenceFluidSolver2D::Tick(const Settings& settings)
    {
        auto N = _pimpl->_dimensions[0];
        assert(_pimpl->_dimensions[1] == _pimpl->_dimensions[0]);

        vel_step( 
            N, 
            _pimpl->_velU.get(), _pimpl->_velV.get(), 
            _pimpl->_prevVelU.get(), _pimpl->_prevVelV.get(), 
            settings._viscosity, settings._deltaTime );
	    dens_step( 
            N, _pimpl->_density.get(), _pimpl->_prevDensity.get(), 
            _pimpl->_velU.get(), _pimpl->_velV.get(), 
            settings._diffusionRate, settings._deltaTime );

        auto eleCount = (_pimpl->_dimensions[0]+2) * (_pimpl->_dimensions[1]+2);
        for (unsigned c=0; c<eleCount; ++c) {
            _pimpl->_prevVelU[c] = 0.f;
            _pimpl->_prevVelV[c] = 0.f;
            _pimpl->_prevDensity[c] = 0.f;
        }
    }

    void ReferenceFluidSolver2D::AddDensity(UInt2 coords, float amount)
    {
        if (coords[0] < _pimpl->_dimensions[0] && coords[1] < _pimpl->_dimensions[1]) {
            unsigned i = (coords[0]+1) + (coords[1]+1) * (_pimpl->_dimensions[0] + 2);
            _pimpl->_prevDensity[i] += amount;
        }
    }

    void ReferenceFluidSolver2D::AddVelocity(UInt2 coords, Float2 vel)
    {
        if (coords[0] < _pimpl->_dimensions[0] && coords[1] < _pimpl->_dimensions[1]) {
            unsigned i = (coords[0]+1) + (coords[1]+1) * (_pimpl->_dimensions[0] + 2);
            _pimpl->_prevVelU[i] += vel[0];
            _pimpl->_prevVelV[i] += vel[1];
        }
    }

    static void RenderFluidDebugging(
        RenderCore::Metal::DeviceContext& metalContext,
        LightingParserContext& parserContext,
        FluidDebuggingMode debuggingMode,
        UInt2 dimensions,
        const float* density, const float* velocityU, const float* velocityV, const float* temperature);

    void ReferenceFluidSolver2D::RenderDebugging(
        RenderCore::Metal::DeviceContext& metalContext,
        LightingParserContext& parserContext,
        FluidDebuggingMode debuggingMode)
    {
        RenderFluidDebugging(
            metalContext, parserContext, debuggingMode,
            _pimpl->_dimensions, _pimpl->_density.get(),
            _pimpl->_velU.get(), _pimpl->_velV.get(),
            nullptr);
    }

    UInt2 ReferenceFluidSolver2D::GetDimensions() const { return _pimpl->_dimensions; }

    ReferenceFluidSolver2D::ReferenceFluidSolver2D(UInt2 dimensions)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_dimensions = dimensions;
        auto eleCount = (dimensions[0]+2) * (dimensions[1]+2);
        _pimpl->_velU = std::make_unique<float[]>(eleCount);
        _pimpl->_velV = std::make_unique<float[]>(eleCount);
        _pimpl->_density = std::make_unique<float[]>(eleCount);
        _pimpl->_prevVelU = std::make_unique<float[]>(eleCount);
        _pimpl->_prevVelV = std::make_unique<float[]>(eleCount);
        _pimpl->_prevDensity = std::make_unique<float[]>(eleCount);

        for (unsigned c=0; c<eleCount; ++c) {
            _pimpl->_velU[c] = 0.f;
            _pimpl->_velV[c] = 0.f;
            _pimpl->_density[c] = 0.f;
            _pimpl->_prevVelU[c] = 0.f;
            _pimpl->_prevVelV[c] = 0.f;
            _pimpl->_prevDensity[c] = 0.f;
        }
    }

    ReferenceFluidSolver2D::~ReferenceFluidSolver2D()
    {
    }


    ReferenceFluidSolver2D::Settings::Settings()
    {
        _deltaTime = 1.0f/60.f;
        _viscosity = 0.f;
        _diffusionRate = 0.f;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    using VectorX = Eigen::VectorXf;
    using MatrixX = Eigen::MatrixXf;
    using VectorField2D = XLEMath::VectorField2DSeparate<VectorX>;
    using VectorField3D = XLEMath::VectorField3DSeparate<VectorX>;
    using ScalarField2D = XLEMath::ScalarField2D<VectorX>;
    using ScalarField3D = XLEMath::ScalarField3D<VectorX>;

    static ScalarField1D AsScalarField1D(VectorX& v) { return ScalarField1D { v.data(), (unsigned)v.size() }; }

    class FluidSolver2D::Pimpl
    {
    public:
        VectorX _velU[3];
        VectorX _velV[3];

        VectorX _density[2];
        VectorX _temperature[2];

        UInt2 _dimensions;

        PoissonSolver _poissonSolver;
        std::shared_ptr<PoissonSolver::PreparedMatrix> _densityDiffusion;
        std::shared_ptr<PoissonSolver::PreparedMatrix> _velocityDiffusion;
        std::shared_ptr<PoissonSolver::PreparedMatrix> _temperatureDiffusion;
        std::shared_ptr<PoissonSolver::PreparedMatrix> _incompressibility;

        float _preparedDensityDiffusion, _preparedVelocityDiffusion, _preparedTemperatureDiffusion;

        void DensityDiffusion(float deltaTime, const FluidSolver2D::Settings& settings);
        void VelocityDiffusion(float deltaTime, const FluidSolver2D::Settings& settings);
        void TemperatureDiffusion(float deltaTime, const FluidSolver2D::Settings& settings);

        void VorticityConfinement(VectorField2D outputField, VectorField2D inputVelocities, float strength, float deltaTime);
        std::shared_ptr<PoissonSolver::PreparedMatrix> BuildDiffusionMethod(float diffusion);
    };

    std::shared_ptr<PoissonSolver::PreparedMatrix> FluidSolver2D::Pimpl::BuildDiffusionMethod(float diffusion)
    {
        const float a0 = 1.f + 4.f * diffusion;
        const float a1 = -diffusion;
        return _poissonSolver.PrepareDiffusionMatrix(a0, a1, PoissonSolver::Method::PreconCG);
    }

    void FluidSolver2D::Pimpl::DensityDiffusion(float deltaTime, const FluidSolver2D::Settings& settings)
    {
        //
        // Diffuse velocity! This is similar to other diffusion operations. 
        // In effect, the values in each cell should slowly "seep" into 
        // neighbour cells -- over time spreading out over the whole grid. 
        // This is diffusion.
        //
        // Mathematically, this operation is called the "heat equation."
        // It is the same equation that is used to model how heat spreads
        // through a room from some source. Actually, it also applies to
        // any radiation (including light).
        //
        // See reference here: https://en.wikipedia.org/wiki/Heat_equation
        // The equation can be written using the laplacian operation. So this
        // is a partial differential equation. We must solve it using an
        // estimate.
        //

        if (!_densityDiffusion || _preparedDensityDiffusion != deltaTime * settings._diffusionRate) {
            _preparedDensityDiffusion = deltaTime * settings._diffusionRate;
            _densityDiffusion = BuildDiffusionMethod(_preparedDensityDiffusion);
        }

        unsigned iterations = 0;
        const bool useGeneralA = false;
        if (constant_expression<useGeneralA>::result()) {
            // iterations = SolvePoisson(_density, AMat, _density, (PossionSolver)settings._diffusionMethod);
        } else {
            iterations = _poissonSolver.Solve(
                AsScalarField1D(_density[0]), 
                *_densityDiffusion,
                AsScalarField1D(_density[0]), 
                (PoissonSolver::Method)settings._diffusionMethod);
        }
        LogInfo << "Density diffusion took: (" << iterations << ") iterations.";
    }

    void FluidSolver2D::Pimpl::VelocityDiffusion(float deltaTime, const FluidSolver2D::Settings& settings)
    {
        if (!_velocityDiffusion || _preparedVelocityDiffusion != deltaTime * settings._viscosity) {
            _preparedVelocityDiffusion = deltaTime * settings._viscosity;
            _velocityDiffusion = BuildDiffusionMethod(_preparedVelocityDiffusion);
        }

        unsigned iterationsu = 0, iterationsv = 0;
        const bool useGeneralA = false;
        if (constant_expression<useGeneralA>::result()) {
            // iterations = SolvePoisson(_density, AMat, _density, (PossionSolver)settings._diffusionMethod);
        } else {
            iterationsu = _poissonSolver.Solve(
                AsScalarField1D(_velU[2]), *_velocityDiffusion, AsScalarField1D(_velU[2]), 
                (PoissonSolver::Method)settings._diffusionMethod);
            iterationsv = _poissonSolver.Solve(
                AsScalarField1D(_velV[2]), *_velocityDiffusion, AsScalarField1D(_velV[2]), 
                (PoissonSolver::Method)settings._diffusionMethod);
        }
        LogInfo << "Velocity diffusion took: (" << iterationsu << ", " << iterationsv << ") iterations.";
    }

    void FluidSolver2D::Pimpl::TemperatureDiffusion(float deltaTime, const FluidSolver2D::Settings& settings)
    {
        if (!_temperatureDiffusion || _preparedTemperatureDiffusion != deltaTime * settings._tempDiffusion) {
            _preparedTemperatureDiffusion = deltaTime * settings._tempDiffusion;
            _temperatureDiffusion = BuildDiffusionMethod(_preparedTemperatureDiffusion);
        }

        unsigned iterations = 0;
        const bool useGeneralA = false;
        if (constant_expression<useGeneralA>::result()) {
            // iterations = SolvePoisson(_temperature, AMat, _temperature, (PossionSolver)settings._diffusionMethod);
        } else {
            iterations = _poissonSolver.Solve(
                AsScalarField1D(_temperature[0]), *_temperatureDiffusion, AsScalarField1D(_temperature[0]), 
                (PoissonSolver::Method)settings._diffusionMethod);
        }
        LogInfo << "Temperature diffusion took: (" << iterations << ") iterations.";
    }

    static void EnforceIncompressibility(
        VectorField2D velField,
        const PoissonSolver& solver, const PoissonSolver::PreparedMatrix& A,
        PoissonSolver::Method method)
    {
        //
        // Following Jos Stam's stable fluids, we'll use Helmholtz-Hodge Decomposition
        // to build a projection operator that will force the velocity field to have
        // zero divergence.
        //
        // This is important for meeting the restrictions from the Naver Stokes equations.
        // 
        // For our input vector field, "w", we can decompose it into two parts:
        //      w = u + del . q        (1)
        // where "u" has zero-divergence (and is our output field). The scalar field,
        // "q" is considered error, and just dropped.
        //
        // We find "q" by multiplying equation (1) by del on both sides, and we get:
        //      del . w = del^2 . q    (2)     (given that del . u is zero)
        //
        // and "u" = w - del . q
        //
        // "b = del^2 . q" is the possion equation; and can be solved in the same way
        // we solve for diffusion (though, the matrix "A" is slightly different). 
        //
        // Following Stam's sample code, we'll do this for both u and v at the same time,
        // with the same solution for "q".
        //
        // Also, here to have to consider how we define the discrete divergence of the
        // field. Stam uses .5f * (f[i+1] - f[i-1]). Depending on how we arrange the physical
        // values with respect to the grid, we could alternatively consider f[i] - f[i-1].
        //

        //
        //  Note -- there may be some benefit in doing this step with a multigrid-like approach
        //          that is, we could solve for incompressibility on a resamples low resolution
        //          grid first; to remove low frequency divergence first... And then successively
        //          use higher resolution grids to remove higher frequency divergence. It might
        //          improve large scale details a bit.
        //

        const auto wh = velField._wh;
        VectorX delW(wh * wh), q(wh * wh);
        q.fill(0.f);
        float N = float(wh);
        for (unsigned y=1; y<wh-1; ++y)
            for (unsigned x=1; x<wh-1; ++x)
                delW[y*wh+x] = 
                    -0.5f/N * 
                    (
                          (*velField._u)[y*wh+x+1]   - (*velField._u)[y*wh+x-1]
                        + (*velField._v)[(y+1)*wh+x] - (*velField._v)[(y-1)*wh+x]
                    );

        SmearBorder2D(delW, wh);
        auto iterations = solver.Solve(
            AsScalarField1D(q), A, AsScalarField1D(delW), 
            method);
        SmearBorder2D(q, wh);

        for (unsigned y=1; y<wh-1; ++y)
            for (unsigned x=1; x<wh-1; ++x) {
                (*velField._u)[y*wh+x] -= .5f*N * (q[y*wh+x+1]   - q[y*wh+x-1]);
                (*velField._v)[y*wh+x] -= .5f*N * (q[(y+1)*wh+x] - q[(y-1)*wh+x]);
            }

        LogInfo << "EnforceIncompressibility took: " << iterations << " iterations.";
    }

    static void EnforceIncompressibility(
        VectorField3D velField,
        const PoissonSolver& solver, const PoissonSolver::PreparedMatrix& A,
        PoissonSolver::Method method)
    {
        const auto dims = velField.Dimensions();
        VectorX delW(dims[0] * dims[1] * dims[2]), q(dims[0] * dims[1] * dims[2]);
        q.fill(0.f);
        Float3 velFieldScale = Float3(float(dims[0]), float(dims[1]), float(dims[2]));
        const UInt3 border(1,1,1);
        for (unsigned z=border[2]; z<dims[2]-border[2]; ++z)
            for (unsigned y=border[1]; y<dims[1]-border[1]; ++y)
                for (unsigned x=border[0]; x<dims[0]-border[0]; ++x) {
                    const auto i = (z*dims[2]+y)*dims[1]+x;
                    delW[i] = 
                        -0.5f * 
                        (
                              ((*velField._u)[i+1]               - (*velField._u)[i-1]) / velFieldScale[0]
                            + ((*velField._v)[i+dims[0]]         - (*velField._v)[i-dims[0]]) / velFieldScale[1]
                            + ((*velField._w)[i+dims[0]*dims[1]] - (*velField._w)[i-dims[0]*dims[1]])  / velFieldScale[2]
                        );
                }

        SmearBorder3D(delW, dims);
        auto iterations = solver.Solve(
            AsScalarField1D(q), A, AsScalarField1D(delW), 
            method);
        SmearBorder3D(q, dims);

        for (unsigned z=border[2]; z<dims[2]-border[2]; ++z)
            for (unsigned y=border[1]; y<dims[1]-border[1]; ++y)
                for (unsigned x=border[0]; x<dims[0]-border[0]; ++x) {
                    const auto i = (z*dims[2]+y)*dims[1]+x;
                    (*velField._u)[i] -= .5f*velFieldScale[0] * (q[i+1]                 - q[i-1]);
                    (*velField._v)[i] -= .5f*velFieldScale[1] * (q[i+dims[0]]           - q[i-dims[0]]);
                    (*velField._w)[i] -= .5f*velFieldScale[2] * (q[i+dims[0]*dims[1]]   - q[i-dims[0]*dims[1]]);
                }

        LogInfo << "EnforceIncompressibility took: " << iterations << " iterations.";
    }

    void FluidSolver2D::Pimpl::VorticityConfinement(
        VectorField2D outputField,
        VectorField2D inputVelocities, float strength, float deltaTime)
    {
        //
        // VorticityConfinement amplifies the existing vorticity at each cell.
        // This is intended to add back errors caused by the discrete equations
        // we're using here.
        //
        // The vorticity can be calculated from the velocity field (by taking the
        // cross product with del. In 2D, this produces a scalar value (which is
        // conceptually a vector in the direction of an imaginary Z axis). We also
        // need to find the divergence of this scalar field.
        //
        // See http://web.stanford.edu/class/cs237d/smoke.pdf for details. In that
        // paper, Fedkiw calculates the vorticity at a half-cell offset from the 
        // velocity field. It's not clear why that was done. We will ignore that, 
        // and calculate vorticity exactly on the velocity field.
        //
        // Note --  like EnforceIncompressibility, it might be helpful to do take a
        //          multigrid-like approach for this step. That is, here we're emphasising
        //          very high frequency vorticity features (because these are the features
        //          that are most likely to be lost in the approximations of the model).
        //          But, by using grids of different resolutions, it might be interesting
        //          to emphasise some higher level vorticity features, as well.
        //

        const auto wh = inputVelocities._wh;
        VectorX vorticity(wh*wh);
        for (unsigned y=1; y<wh-1; ++y)
            for (unsigned x=1; x<wh-1; ++x) {
                auto dvydx = .5f * inputVelocities.Load(UInt2(x+1, y))[1] - inputVelocities.Load(UInt2(x-1, y))[1];
                auto dvxdy = .5f * inputVelocities.Load(UInt2(x, y+1))[0] - inputVelocities.Load(UInt2(x, y-1))[0];
                vorticity[y*wh+x] = dvydx - dvxdy;
            }
        SmearBorder2D(vorticity, wh);

        const float s = deltaTime * strength * float(wh-2);
        for (unsigned y=1; y<wh-1; ++y)
            for (unsigned x=1; x<wh-1; ++x) {
                    // find the discrete divergence of the absolute vorticity field
                Float2 div(
                    .5f * (XlAbs(vorticity[y*wh+x+1]) - XlAbs(vorticity[y*wh+x-1])),
                    .5f * (XlAbs(vorticity[(y+1)*wh+x]) - XlAbs(vorticity[(y-1)*wh+x])));

                float magSq = MagnitudeSquared(div);
                if (magSq > 1e-10f) {
                    div *= XlRSqrt(magSq);

                        // in 2D, the vorticity is in the Z direction. Which means the cross product
                        // with our divergence vector is simple
                    float omega = vorticity[y*wh+x];
                    Float2 additionalVel(s * div[1] * omega, s * -div[0] * omega);

                    outputField.Write(
                        UInt2(x, y),
                        outputField.Load(UInt2(x, y)) + additionalVel);
                }
            }
    }

    void FluidSolver2D::Tick(float deltaTime, const Settings& settings)
    {
        auto D = _pimpl->_dimensions[0];
        assert(_pimpl->_dimensions[1] == _pimpl->_dimensions[0]);

        float dt = deltaTime;
        auto wh = D+2;
        auto N = wh*wh;

        auto& velUT0 = _pimpl->_velU[0];
        auto& velUT1 = _pimpl->_velU[1];
        auto& velUSrc = _pimpl->_velU[2];
        auto& velUWorking = _pimpl->_velU[2];

        auto& velVT0 = _pimpl->_velV[0];
        auto& velVT1 = _pimpl->_velV[1];
        auto& velVSrc = _pimpl->_velV[2];
        auto& velVWorking = _pimpl->_velV[2];

        auto& densitySrc = _pimpl->_density[0];
        auto& densityWorking = _pimpl->_density[0];
        auto& densityT1 = _pimpl->_density[1];

        auto& temperatureSrc = _pimpl->_temperature[0];
        auto& temperatureWorking = _pimpl->_temperature[0];
        auto& temperatureT1 = _pimpl->_temperature[1];

        _pimpl->VorticityConfinement(
            VectorField2D(&velUSrc, &velVSrc, D+2),
            VectorField2D(&velUT1, &velVT1, D+2),           // last frame results
            settings._vorticityConfinement, deltaTime);

                    // buoyancy force
        const float buoyancyAlpha = settings._buoyancyAlpha;
        const float buoyancyBeta = settings._buoyancyBeta;
        for (unsigned y=1; y<wh-1; ++y)
            for (unsigned x=1; x<wh-1; ++x) {
                unsigned i=y*wh+x;
                velVSrc[i] -=     // (upwards is -1 in V)
                     -buoyancyAlpha * densityT1[i]
                    + buoyancyBeta  * temperatureT1[i];       // temperature field is just the difference from ambient
            }

        for (unsigned c=0; c<N; ++c) {
            velUT0[c] = velUT1[c];
            velVT0[c] = velVT1[c];
            velUWorking[c] = velUT1[c] + dt * velUSrc[c];
            velVWorking[c] = velVT1[c] + dt * velVSrc[c];

            densityWorking[c] = densityT1[c] + dt * densitySrc[c];
            temperatureWorking[c] = temperatureT1[c] + dt * temperatureSrc[c];
        }

        _pimpl->VelocityDiffusion(deltaTime, settings);

        AdvectionSettings advSettings { (AdvectionMethod)settings._advectionMethod, (AdvectionInterpolationMethod)settings._interpolationMethod, settings._advectionSteps };
        PerformAdvection(
            VectorField2D(&velUT1,      &velVT1,        D+2),
            VectorField2D(&velUWorking, &velVWorking,   D+2),
            VectorField2D(&velUT0,      &velVT0,        D+2),
            VectorField2D(&velUWorking, &velVWorking,   D+2),
            deltaTime, advSettings);
        
        ReflectUBorder2D(velUT1, D+2);
        ReflectVBorder2D(velVT1, D+2);
        EnforceIncompressibility(
            VectorField2D(&velUT1, &velVT1, D+2),
            _pimpl->_poissonSolver, *_pimpl->_incompressibility,
            (PoissonSolver::Method)settings._enforceIncompressibilityMethod);

        _pimpl->DensityDiffusion(deltaTime, settings);
        PerformAdvection(
            ScalarField2D(&densityT1, D+2),
            ScalarField2D(&densityWorking, D+2),
            VectorField2D(&velUT0, &velVT0, D+2),
            VectorField2D(&velUT1, &velVT1, D+2),
            deltaTime, advSettings);

        _pimpl->TemperatureDiffusion(deltaTime, settings);
        PerformAdvection(
            ScalarField2D(&temperatureT1, D+2),
            ScalarField2D(&temperatureWorking, D+2),
            VectorField2D(&velUT0, &velVT0, D+2),
            VectorField2D(&velUT1, &velVT1, D+2),
            deltaTime, advSettings);

        for (unsigned c=0; c<N; ++c) {
            velUSrc[c] = 0.f;
            velVSrc[c] = 0.f;
            densitySrc[c] = 0.f;
            temperatureSrc[c] = 0.f;
        }
    }

    void FluidSolver2D::AddDensity(UInt2 coords, float amount)
    {
        if (coords[0] < _pimpl->_dimensions[0] && coords[1] < _pimpl->_dimensions[1]) {
            unsigned i = (coords[0]+1) + (coords[1]+1) * (_pimpl->_dimensions[0] + 2);
            _pimpl->_density[0][i] += amount;
        }
    }

    void FluidSolver2D::AddTemperature(UInt2 coords, float amount)
    {
        if (coords[0] < _pimpl->_dimensions[0] && coords[1] < _pimpl->_dimensions[1]) {
            unsigned i = (coords[0]+1) + (coords[1]+1) * (_pimpl->_dimensions[0] + 2);
            // _pimpl->_prevTemperature[i] += amount;

                // heat up to approach this temperature
            auto oldTemp = _pimpl->_temperature[1][i];
            _pimpl->_temperature[1][i] = std::max(oldTemp, LinearInterpolate(oldTemp, amount, 0.5f));
        }
    }

    void FluidSolver2D::AddVelocity(UInt2 coords, Float2 vel)
    {
        if (coords[0] < _pimpl->_dimensions[0] && coords[1] < _pimpl->_dimensions[1]) {
            unsigned i = (coords[0]+1) + (coords[1]+1) * (_pimpl->_dimensions[0] + 2);
            _pimpl->_velU[2][i] += vel[0];
            _pimpl->_velV[2][i] += vel[1];
        }
    }

    void FluidSolver2D::RenderDebugging(
        RenderCore::Metal::DeviceContext& metalContext,
        LightingParserContext& parserContext,
        FluidDebuggingMode debuggingMode)
    {
        RenderFluidDebugging(
            metalContext, parserContext, debuggingMode,
            _pimpl->_dimensions, _pimpl->_density[1].data(),
            _pimpl->_velU[1].data(), _pimpl->_velV[1].data(),
            _pimpl->_temperature[1].data());
    }

    UInt2 FluidSolver2D::GetDimensions() const { return _pimpl->_dimensions; }

    FluidSolver2D::FluidSolver2D(UInt2 dimensions)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_dimensions = dimensions;
        auto N = (dimensions[0]+2) * (dimensions[1]+2);

        for (unsigned c=0; c<dimof(_pimpl->_velU); ++c) {
            _pimpl->_velU[c] = VectorX(N);
            _pimpl->_velV[c] = VectorX(N);
            _pimpl->_velU[c].fill(0.f);
            _pimpl->_velV[c].fill(0.f);
        }

        for (unsigned c=0; c<dimof(_pimpl->_density); ++c) {
            _pimpl->_density[c] = VectorX(N);
            _pimpl->_temperature[c] = VectorX(N);
            _pimpl->_density[c].fill(0.f);
            _pimpl->_temperature[c].fill(0.f);
        }

        // const float dt = 1.0f / 60.f;
        // float a = 5.f * dt;

        // auto wh = _pimpl->_dimensions[0]+2;
        // auto AMat = [wh, a](unsigned i, unsigned j)
        //     {
        //         if (i == j) return 1.f + 4.f*a;
        //         // auto x0 = (i%wh), y0 = i/wh;
        //         // auto x1 = (j%wh), y1 = j/wh;
        //         // if (    (std::abs(int(x0)-int(x1)) == 1 && y0 == y1)
        //         //     ||  (std::abs(int(y0)-int(y1)) == 1 && x0 == x1)) {
        //         if (j==(i+1) || j==(i-1) || j==(i+wh) || j == (i-wh))
        //             return -a;   
        //         return 0.f;
        //     };
        // 
        // _pimpl->AMat = std::function<float(unsigned, unsigned)>(AMat);
        
        #if defined(_DEBUG)
            // {
            //     auto comparePrecon = CalculateIncompleteCholesky(AMat2D { wh, 1.f + 4.f * a, -a }, N, 0);
            //     float maxDiff = 0.f;
            //     for (unsigned y=0; y<5; ++y)
            //         for (unsigned x=0; x<wh; ++x) {
            //             auto diff = XlAbs(comparePrecon(x, y) - bandedPrecon(x, y));
            //             diff = std::max(diff, maxDiff);
            //         }
            //     LogInfo << "Preconditioner matrix error: " << maxDiff;
            // }

            // for (unsigned i=0; i<N; ++i)
            //     LogInfo << bandedPrecon(i, 0) << ", " << bandedPrecon(i, 1) << ", " << bandedPrecon(i, 2) << ", " << bandedPrecon(i, 3) << ", " << bandedPrecon(i, 4);
        
            // {
            //     auto fullPrecon = CalculateIncompleteCholesky(_pimpl->AMat, N);
            //     for (unsigned i=0; i<N; ++i) {
            //         int j2[] = { 
            //             int(i) + _pimpl->_bands[0], 
            //             int(i) + _pimpl->_bands[1],
            //             int(i) + _pimpl->_bands[2], 
            //             int(i) + _pimpl->_bands[3], 
            //             int(i) + _pimpl->_bands[4]
            //         };
            //         for (unsigned j=0; j<dimof(j2); ++j) {
            //             if (j2[j] >= 0 && j2[j] < int(N)) {
            //                 float a = bandedPrecon(i, j);
            //                 float b = fullPrecon(i, j2[j]);
            //                 assert(Equivalent(a, b, 1.e-3f));
            //             }
            //         }
            //     }
            // }
        #endif

        // _pimpl->_bandedPrecon = SparseBandedMatrix(std::move(bandedPrecon), _pimpl->_bands, dimof(_pimpl->_bands));

        UInt2 fullDims(dimensions[0]+2, dimensions[1]+2);
        _pimpl->_poissonSolver = PoissonSolver(2, &fullDims[0]);
        _pimpl->_incompressibility = _pimpl->_poissonSolver.PrepareDivergenceMatrix(PoissonSolver::Method::PreconCG);

        _pimpl->_preparedDensityDiffusion = 0.f;
        _pimpl->_preparedVelocityDiffusion = 0.f;
        _pimpl->_preparedTemperatureDiffusion = 0.f;
    }

    FluidSolver2D::~FluidSolver2D(){}

    FluidSolver2D::Settings::Settings()
    {
        _viscosity = 0.05f;
        _diffusionRate = 0.05f;
        _tempDiffusion = 2.f;
        _diffusionMethod = 0;
        _advectionMethod = 3;
        _advectionSteps = 4;
        _enforceIncompressibilityMethod = 3;
        _buoyancyAlpha = 2.f;
        _buoyancyBeta = 2.2f;
        _addDensity = 1.f;
        _addTemperature = 0.25f;
        _vorticityConfinement = 0.75f;
        _interpolationMethod = 0;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class FluidSolver3D::Pimpl
    {
    public:
        VectorX _velU[3];
        VectorX _velV[3];
        VectorX _velW[3];
        VectorX _density[2];

        UInt3 _dimsWithoutBorder;
        UInt3 _dimsWithBorder;
        unsigned _N;

        PoissonSolver _poissonSolver;
        std::shared_ptr<PoissonSolver::PreparedMatrix> _densityDiffusion;
        std::shared_ptr<PoissonSolver::PreparedMatrix> _velocityDiffusion;
        std::shared_ptr<PoissonSolver::PreparedMatrix> _incompressibility;

        float _preparedDensityDiffusion, _preparedVelocityDiffusion;

        void DensityDiffusion(float deltaTime, const Settings& settings);
        void VelocityDiffusion(float deltaTime, const Settings& settings);
        std::shared_ptr<PoissonSolver::PreparedMatrix> BuildDiffusionMethod(float diffusion);
    };

    void FluidSolver3D::Tick(float deltaTime, const Settings& settings)
    {
        float dt = deltaTime;
        const auto N = _pimpl->_N;
        auto& velUT0 = _pimpl->_velU[0];
        auto& velUT1 = _pimpl->_velU[1];
        auto& velUSrc = _pimpl->_velU[2];
        auto& velUWorking = _pimpl->_velU[2];

        auto& velVT0 = _pimpl->_velV[0];
        auto& velVT1 = _pimpl->_velV[1];
        auto& velVSrc = _pimpl->_velV[2];
        auto& velVWorking = _pimpl->_velV[2];

        auto& velWT0 = _pimpl->_velW[0];
        auto& velWT1 = _pimpl->_velW[1];
        auto& velWSrc = _pimpl->_velW[2];
        auto& velWWorking = _pimpl->_velW[2];

        auto& densitySrc = _pimpl->_density[0];
        auto& densityWorking = _pimpl->_density[0];
        auto& densityT1 = _pimpl->_density[1];

        for (unsigned c=0; c<N; ++c) {
            velUT0[c] = velUT1[c];
            velVT0[c] = velVT1[c];
            velWT0[c] = velWT1[c];
            velUWorking[c] = velUT1[c] + dt * velUSrc[c];
            velVWorking[c] = velVT1[c] + dt * velVSrc[c];
            densityWorking[c] = densityT1[c] + dt * densitySrc[c];
        }

        _pimpl->VelocityDiffusion(deltaTime, settings);

        AdvectionSettings advSettings { (AdvectionMethod)settings._advectionMethod, (AdvectionInterpolationMethod)settings._interpolationMethod, settings._advectionSteps };
        PerformAdvection(
            VectorField3D(&velUT1,      &velVT1,        &velWT1,        _pimpl->_dimsWithBorder),
            VectorField3D(&velUWorking, &velVWorking,   &velWWorking,   _pimpl->_dimsWithBorder),
            VectorField3D(&velUT0,      &velVT0,        &velWT0,        _pimpl->_dimsWithBorder),
            VectorField3D(&velUWorking, &velVWorking,   &velWWorking,   _pimpl->_dimsWithBorder),
            deltaTime, advSettings);
        
        ReflectUBorder2D(velUT1, _pimpl->_dimsWithBorder[0]);
        ReflectVBorder2D(velVT1, _pimpl->_dimsWithBorder[0]);
        EnforceIncompressibility(
            VectorField3D(&velUT1, &velVT1, &velWT1, _pimpl->_dimsWithBorder),
            _pimpl->_poissonSolver, *_pimpl->_incompressibility,
            (PoissonSolver::Method)settings._enforceIncompressibilityMethod);

        _pimpl->DensityDiffusion(deltaTime, settings);
        PerformAdvection(
            ScalarField3D(&densityT1, _pimpl->_dimsWithBorder),
            ScalarField3D(&densityWorking, _pimpl->_dimsWithBorder),
            VectorField3D(&velUT0, &velVT0, &velWT0, _pimpl->_dimsWithBorder),
            VectorField3D(&velUT1, &velVT1, &velWT1, _pimpl->_dimsWithBorder),
            deltaTime, advSettings);

        for (unsigned c=0; c<N; ++c) {
            velUSrc[c] = 0.f;
            velVSrc[c] = 0.f;
            velWSrc[c] = 0.f;
            densitySrc[c] = 0.f;
        }
    }

    std::shared_ptr<PoissonSolver::PreparedMatrix> FluidSolver3D::Pimpl::BuildDiffusionMethod(float diffusion)
    {
        const float a0 = 1.f + 4.f * diffusion;
        const float a1 = -diffusion;
        return _poissonSolver.PrepareDiffusionMatrix(a0, a1, PoissonSolver::Method::PreconCG);
    }

    void FluidSolver3D::Pimpl::DensityDiffusion(float deltaTime, const Settings& settings)
    {
        if (!_densityDiffusion || _preparedDensityDiffusion != deltaTime * settings._diffusionRate) {
            _preparedDensityDiffusion = deltaTime * settings._diffusionRate;
            _densityDiffusion = BuildDiffusionMethod(_preparedDensityDiffusion);
        }

        auto iterations = _poissonSolver.Solve(
            AsScalarField1D(_density[0]), 
            *_densityDiffusion,
            AsScalarField1D(_density[0]), 
            (PoissonSolver::Method)settings._diffusionMethod);
        LogInfo << "Density diffusion took: (" << iterations << ") iterations.";
    }

    void FluidSolver3D::Pimpl::VelocityDiffusion(float deltaTime, const Settings& settings)
    {
        if (!_velocityDiffusion || _preparedVelocityDiffusion != deltaTime * settings._viscosity) {
            _preparedVelocityDiffusion = deltaTime * settings._viscosity;
            _velocityDiffusion = BuildDiffusionMethod(_preparedVelocityDiffusion);
        }

        auto iterationsu = _poissonSolver.Solve(
            AsScalarField1D(_velU[2]), *_velocityDiffusion, AsScalarField1D(_velU[2]), 
            (PoissonSolver::Method)settings._diffusionMethod);
        auto iterationsv = _poissonSolver.Solve(
            AsScalarField1D(_velV[2]), *_velocityDiffusion, AsScalarField1D(_velV[2]), 
            (PoissonSolver::Method)settings._diffusionMethod);
        LogInfo << "Velocity diffusion took: (" << iterationsu << ", " << iterationsv << ") iterations.";
    }

    UInt3 FluidSolver3D::GetDimensions() const { return _pimpl->_dimsWithoutBorder; }

    FluidSolver3D::FluidSolver3D(UInt3 dimensions)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_dimsWithoutBorder = dimensions;
        _pimpl->_dimsWithBorder = dimensions + UInt3(2, 2, 2);
        auto N = _pimpl->_dimsWithBorder[0] * _pimpl->_dimsWithBorder[1] * _pimpl->_dimsWithBorder[2];
        _pimpl->_N = N;

        for (unsigned c=0; c<dimof(_pimpl->_velU); ++c) {
            _pimpl->_velU[c] = VectorX(N);
            _pimpl->_velV[c] = VectorX(N);
            _pimpl->_velW[c] = VectorX(N);
            _pimpl->_velU[c].fill(0.f);
            _pimpl->_velV[c].fill(0.f);
            _pimpl->_velW[c].fill(0.f);
        }

        for (unsigned c=0; c<dimof(_pimpl->_density); ++c) {
            _pimpl->_density[c] = VectorX(N);
            _pimpl->_density[c].fill(0.f);
        }

        UInt3 fullDims(dimensions[0]+2, dimensions[1]+2, dimensions[2]+2);
        _pimpl->_poissonSolver = PoissonSolver(3, &fullDims[0]);
        _pimpl->_incompressibility = _pimpl->_poissonSolver.PrepareDivergenceMatrix(
            PoissonSolver::Method::PreconCG);

        _pimpl->_preparedDensityDiffusion = 0.f;
        _pimpl->_preparedVelocityDiffusion = 0.f;
    }

    FluidSolver3D::~FluidSolver3D() {}

}

    // for draw debugging...
#include "SceneEngineUtils.h"
#include "LightingParserContext.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/DataPacket.h"
#include "../BufferUploads/ResourceLocator.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Buffer.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/ShaderResource.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Techniques.h"

namespace SceneEngine
{
    static void RenderFluidDebugging(
        RenderCore::Metal::DeviceContext& metalContext,
        LightingParserContext& parserContext,
        FluidDebuggingMode debuggingMode,
        UInt2 dimensions,
        const float* density, const float* velocityU, const float* velocityV, const float* temperature)
    {
        TRY {
            using namespace RenderCore;
            using namespace BufferUploads;
            auto& uploads = GetBufferUploads();

            auto dx = dimensions[0], dy = dimensions[1];

            auto desc = CreateDesc(
                BindFlag::ShaderResource,
                0, GPUAccess::Read|GPUAccess::Write,
                TextureDesc::Plain2D(dx+2, dy+2, RenderCore::Metal::NativeFormat::R32_FLOAT),
                "fluid");
            auto densityPkt = CreateBasicPacket((dx+2)*(dy+2)*sizeof(float), density, TexturePitches((dx+2)*sizeof(float), (dy+2)*(dx+2)*sizeof(float)));
            auto velUPkt = CreateBasicPacket((dx+2)*(dy+2)*sizeof(float), velocityU, TexturePitches((dx+2)*sizeof(float), (dy+2)*(dx+2)*sizeof(float)));
            auto velVPkt = CreateBasicPacket((dx+2)*(dy+2)*sizeof(float), velocityV, TexturePitches((dx+2)*sizeof(float), (dy+2)*(dx+2)*sizeof(float)));
            auto temperaturePkt = CreateBasicPacket((dx+2)*(dy+2)*sizeof(float), temperature, TexturePitches((dx+2)*sizeof(float), (dy+2)*(dx+2)*sizeof(float)));

            auto density = uploads.Transaction_Immediate(desc, densityPkt.get());
            auto velU = uploads.Transaction_Immediate(desc, velUPkt.get());
            auto velV = uploads.Transaction_Immediate(desc, velVPkt.get());
            auto temperature = uploads.Transaction_Immediate(desc, temperaturePkt.get());

            metalContext.BindPS(
                MakeResourceList(
                    Metal::ShaderResourceView(density->GetUnderlying()),
                    Metal::ShaderResourceView(velU->GetUnderlying()),
                    Metal::ShaderResourceView(velV->GetUnderlying()),
                    Metal::ShaderResourceView(temperature->GetUnderlying())));

            const ::Assets::ResChar* pixelShader = "";
            if (debuggingMode == FluidDebuggingMode::Density) {
                pixelShader = "game/xleres/cfd/debug.sh:ps_density:ps_*";
            } else if (debuggingMode == FluidDebuggingMode::Temperature) {
                pixelShader = "game/xleres/cfd/debug.sh:ps_temperature:ps_*";
            } else if (debuggingMode == FluidDebuggingMode::Velocity) {
                pixelShader = "game/xleres/cfd/debug.sh:ps_velocity:ps_*";
            }

            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "game/xleres/basic3D.vsh:PT:vs_*", pixelShader);

            Float2 wsDims = dimensions;

            struct Vertex { Float3 position; Float2 texCoord; } 
            vertices[] = 
            {
                { Float3(0.f, 0.f, 0.f), Float2(0.f, 0.f) },
                { Float3(wsDims[0], 0.f, 0.f), Float2(1.f, 0.f) },
                { Float3(0.f, wsDims[1], 0.f), Float2(0.f, 1.f) },
                { Float3(wsDims[0], wsDims[1], 0.f), Float2(1.f, 1.f) }
            };

            Metal::BoundInputLayout inputLayout(Metal::GlobalInputLayouts::PT, shader);
            Metal::BoundUniforms uniforms(shader);
            Techniques::TechniqueContext::BindGlobalUniforms(uniforms);
                
            metalContext.BindPS(MakeResourceList(Techniques::CommonResources()._defaultSampler, Techniques::CommonResources()._linearClampSampler));
            metalContext.Bind(inputLayout);
            uniforms.Apply(metalContext, 
                parserContext.GetGlobalUniformsStream(), Metal::UniformsStream());
            metalContext.Bind(shader);

            metalContext.Bind(MakeResourceList(
                Metal::VertexBuffer(vertices, sizeof(vertices))), sizeof(Vertex), 0);
            metalContext.Bind(Techniques::CommonResources()._cullDisable);
            metalContext.Bind(Metal::Topology::TriangleStrip);
            metalContext.Draw(4);
        } 
        CATCH (const ::Assets::Exceptions::PendingAsset& e) { parserContext.Process(e); }
        CATCH (const ::Assets::Exceptions::InvalidAsset& e) { parserContext.Process(e); }
        CATCH_END
    }
}

template<> const ClassAccessors& GetAccessors<SceneEngine::ReferenceFluidSolver2D::Settings>()
{
    using Obj = SceneEngine::ReferenceFluidSolver2D::Settings;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add(u("DeltaTime"), DefaultGet(Obj, _deltaTime),  DefaultSet(Obj, _deltaTime));
        props.Add(u("Viscosity"), DefaultGet(Obj, _viscosity),  DefaultSet(Obj, _viscosity));
        props.Add(u("DiffusionRate"), DefaultGet(Obj, _diffusionRate),  DefaultSet(Obj, _diffusionRate));
        init = true;
    }
    return props;
}

template<> const ClassAccessors& GetAccessors<SceneEngine::FluidSolver2D::Settings>()
{
    using Obj = SceneEngine::FluidSolver2D::Settings;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add(u("Viscosity"), DefaultGet(Obj, _viscosity),  DefaultSet(Obj, _viscosity));
        props.Add(u("DiffusionRate"), DefaultGet(Obj, _diffusionRate),  DefaultSet(Obj, _diffusionRate));
        props.Add(u("TempDiffusionRate"), DefaultGet(Obj, _tempDiffusion),  DefaultSet(Obj, _tempDiffusion));
        props.Add(u("DiffusionMethod"), DefaultGet(Obj, _diffusionMethod),  DefaultSet(Obj, _diffusionMethod));
        props.Add(u("AdvectionMethod"), DefaultGet(Obj, _advectionMethod),  DefaultSet(Obj, _advectionMethod));
        props.Add(u("AdvectionSteps"), DefaultGet(Obj, _advectionSteps),  DefaultSet(Obj, _advectionSteps));
        props.Add(u("EnforceIncompressibility"), DefaultGet(Obj, _enforceIncompressibilityMethod),  DefaultSet(Obj, _enforceIncompressibilityMethod));
        props.Add(u("BouyancyAlpha"), DefaultGet(Obj, _buoyancyAlpha),  DefaultSet(Obj, _buoyancyAlpha));
        props.Add(u("BouyancyBeta"), DefaultGet(Obj, _buoyancyBeta),  DefaultSet(Obj, _buoyancyBeta));
        props.Add(u("AddDensity"), DefaultGet(Obj, _addDensity),  DefaultSet(Obj, _addDensity));
        props.Add(u("AddTemperature"), DefaultGet(Obj, _addTemperature),  DefaultSet(Obj, _addTemperature));
        props.Add(u("VorticityConfinement"), DefaultGet(Obj, _vorticityConfinement),  DefaultSet(Obj, _vorticityConfinement));
        props.Add(u("InterpolationMethod"), DefaultGet(Obj, _interpolationMethod),  DefaultSet(Obj, _interpolationMethod));
        init = true;
    }
    return props;
}
