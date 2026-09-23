#include "stdafx.h"
#pragma hdrstop

#include "ParticleEffect.h"
#include "bgfxParticleRender.h"

using namespace PAPI;
using namespace PS;

const u32	PS::uDT_STEP 	= 33;
const float	PS::fDT_STEP 	= float(uDT_STEP)/1000.f;

void PS::OnEffectParticleBirth(void* owner, u32 , PAPI::Particle& m, u32 )
{
	CParticleEffect* PE = static_cast<CParticleEffect*>(owner);
	VERIFY(PE);
    CPEDef* PED			= PE->GetDefinition();

	if (PED)
	{
        if (PED->m_Flags.is(CPEDef::dfRandomFrame))
            m.frame	= (u16)iFloor(Random.randI(PED->m_Frame.m_iFrameCount)*255.f);
        if (PED->m_Flags.is(CPEDef::dfAnimated)&&PED->m_Flags.is(CPEDef::dfRandomPlayback)&&Random.randI(2))
            m.flags.set(Particle::ANIMATE_CCW,TRUE);
    }
}

void PS::OnEffectParticleDead(void* , u32 , PAPI::Particle& , u32 )
{
}

CParticleEffect::CParticleEffect()
{
	m_HandleEffect = ParticleManager()->CreateEffect(1);
	VERIFY(m_HandleEffect>=0);
	m_HandleActionList = ParticleManager()->CreateActionList();
	VERIFY(m_HandleActionList>=0);
	m_RT_Flags.zero			();
	m_Def					= 0;
	m_BlendMode				= bgfxParticles::BLEND_BLEND;
	m_fElapsedLimit			= 0.f;
	m_MemDT					= 0;
	m_InitialPosition.set	(0,0,0);
	m_DestroyCallback		= 0;
	m_CollisionCallback		= 0;
	m_XFORM.identity		();
}

CParticleEffect::~CParticleEffect()
{
	OnDeviceDestroy			();
	ParticleManager()->DestroyEffect		(m_HandleEffect);
	ParticleManager()->DestroyActionList	(m_HandleActionList);
}

void CParticleEffect::Play()
{
	static bool s_loggedPlay = false;
	if (!s_loggedPlay) { s_loggedPlay = true; LogInfo("[BGFX] PEffect Play"); }
	m_RT_Flags.set		(flRT_DefferedStop,FALSE);
	m_RT_Flags.set		(flRT_Playing,TRUE);
    ParticleManager()->PlayEffect(m_HandleEffect,m_HandleActionList);
}

void CParticleEffect::Stop(BOOL bDefferedStop)
{
    ParticleManager()->StopEffect(m_HandleEffect,m_HandleActionList,bDefferedStop);
	if (bDefferedStop)
	{
		m_RT_Flags.set	(flRT_DefferedStop,TRUE);
	}
	else
	{
		m_RT_Flags.set	(flRT_Playing,FALSE);
	}
}

void CParticleEffect::RefreshShader()
{
}

void CParticleEffect::UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM)
{
	m_RT_Flags.set			(flRT_XFORM, bXFORM);

	if (bXFORM)
		m_XFORM.set	(m);
	else
	{
		m_InitialPosition	= m.c;
        ParticleManager()->Transform(m_HandleActionList,m,velocity);
	}
}

void CParticleEffect::OnFrame(u32 frame_dt)
{
	if (m_Def && m_RT_Flags.is(flRT_Playing))
	{
		m_MemDT			+= frame_dt;

		int	StepCount	= 0;
		if (m_MemDT >= uDT_STEP)
		{
			StepCount	= m_MemDT/uDT_STEP;
			m_MemDT		= m_MemDT%uDT_STEP;
			clamp		(StepCount,0,3);
		}

		for (;StepCount; StepCount--)
		{
			if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
			{
				if (!m_RT_Flags.is(flRT_DefferedStop))
				{
					m_fElapsedLimit -= fDT_STEP;
					if (m_fElapsedLimit < 0.f)
					{
						m_fElapsedLimit = m_Def->m_fTimeLimit;
						Stop		(true);
                        break;
					}
				}
			}
            ParticleManager()->Update(m_HandleEffect,m_HandleActionList,fDT_STEP);

            PAPI::Particle* particles;
            u32 p_cnt;
            ParticleManager()->GetParticles(m_HandleEffect,particles,p_cnt);

			if (m_Def->m_Flags.is(CPEDef::dfFramed|CPEDef::dfAnimated))
				m_Def->ExecuteAnimate	(particles,p_cnt,fDT_STEP);

			if (m_Def->m_Flags.is(CPEDef::dfCollision))
				m_Def->ExecuteCollision	(particles,p_cnt,fDT_STEP,this,m_CollisionCallback);

			if (p_cnt)
			{
				vis.box.invalidate	();
				float p_size = 0.f;
				for (u32 i = 0; i < p_cnt; i++)
				{
					Particle &m 	= particles[i];
					vis.box.modify((Fvector&)m.pos);

					if (m.size.x>p_size)
						p_size = m.size.x;
					if (m.size.y>p_size)
						p_size = m.size.y;
					if (m.size.z>p_size)
						p_size = m.size.z;
				}
				vis.box.grow		(p_size);
				vis.box.getsphere	(vis.sphere.P,vis.sphere.R);
			}
			if (m_RT_Flags.is(flRT_DefferedStop) && (0 == p_cnt))
			{
				m_RT_Flags.set		(flRT_Playing|flRT_DefferedStop,FALSE);
				break;
			}
		}
	}
	else
	{
		vis.box.set			(m_InitialPosition,m_InitialPosition);
		vis.box.grow		(EPS_L);
		vis.box.getsphere	(vis.sphere.P,vis.sphere.R);
	}
}

BOOL CParticleEffect::Compile(CPEDef* def)
{
	m_Def 						= def;
	if (m_Def)
	{
		IReader F				(m_Def->m_Actions.pointer(),m_Def->m_Actions.size());
        ParticleManager()->LoadActions		(m_HandleActionList,F);
        ParticleManager()->SetMaxParticles	(m_HandleEffect,m_Def->m_MaxParticles);
        ParticleManager()->SetCallback		(m_HandleEffect,OnEffectParticleBirth,OnEffectParticleDead,this,0);
		if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
			m_fElapsedLimit 	= m_Def->m_fTimeLimit;
	}
	if (def)
	{
		shader = def->m_CachedShader;
		m_BlendMode = bgfxParticles::BlendFromShaderName(def->m_ShaderName.c_str());
	}

	return TRUE;
}

void CParticleEffect::SetBirthDeadCB(PAPI::OnBirthParticleCB bc, PAPI::OnDeadParticleCB dc, void* owner, u32 p)
{
    ParticleManager()->SetCallback		(m_HandleEffect,bc,dc,owner,p);
}

u32 CParticleEffect::ParticlesCount()
{
	return ParticleManager()->GetParticlesCount(m_HandleEffect);
}

void CParticleEffect::Copy(dxRender_Visual* )
{
	FATAL	("Can't duplicate particle system - NOT IMPLEMENTED");
}

void CParticleEffect::Render(float)
{
	PAPI::Particle* particles;
	u32 p_cnt;
	ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);
	if (p_cnt && m_Def)
		bgfxParticles::SubmitPAPI(particles, p_cnt, m_Def, m_BlendMode,
			m_RT_Flags.is(flRT_XFORM) ? &m_XFORM : nullptr);
}

void CParticleEffect::OnDeviceCreate()
{
}

void CParticleEffect::OnDeviceDestroy()
{
}
