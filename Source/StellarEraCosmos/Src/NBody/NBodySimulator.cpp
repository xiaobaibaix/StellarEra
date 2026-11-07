// N 体引力求解器 —— 实现。

#include "NBodySimulator.h"

// TWeakObjectPtr<AActor> 的成员函数实例化需要 AActor 完整定义，显式引入以免换 PCH 策略后编不过。
#include "GameFramework/Actor.h"

FNBodySimulator::FNBodySimulator()
{
	MarkEnergyBaseline();
}

///////////////////////////////////////////////////////////////////////////////
// 参数

void FNBodySimulator::SetUnitSystem(ECosmosUnitSystem Unit)
{
	UnitSystem = Unit;
}

void FNBodySimulator::SetSofteningLength(double InSoftening)
{
	SofteningLength = FMath::Max(InSoftening, 1e-12);
}

void FNBodySimulator::SetCentimetersPerAu(double InCentimetersPerAu)
{
	// 给一个下限：0 会让所有天体坍缩到原点，负数更无意义。
	CentimetersPerAu = FMath::Max(InCentimetersPerAu, 1.0);
}

///////////////////////////////////////////////////////////////////////////////
// 天体管理

int32 FNBodySimulator::AddBody(const FCosmosBodyState& Body)
{
	const int32 Index = Bodies.Add(Body);
	Accelerations.Add(FVector3d::ZeroVector);
	ComputeAccelerations();
	return Index;
}

int32 FNBodySimulator::FindBodyByActor(const AActor* Actor) const
{
	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		if (Bodies[i].OwnerActor.Get() == Actor)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// 引力计算

double FNBodySimulator::GetG() const
{
	return (UnitSystem == ECosmosUnitSystem::Game)
		? CosmosUnits::GravitationalConstant_Game
		: CosmosUnits::GravitationalConstant_SI;
}

void FNBodySimulator::ComputeAccelerations()
{
	const int32 N = Bodies.Num();
	Accelerations.SetNumZeroed(N);

	const double G = GetG();

	const double CutoffSq = (GravityCutoff > 0.0) ? GravityCutoff * GravityCutoff : 0.0;

	for (int32 i = 0; i < N; ++i)
	{
		FVector3d Sum = FVector3d::ZeroVector;

		for (int32 j = 0; j < N; ++j)
		{
			if (i == j)
			{
				continue;
			}

			const FVector3d Delta = Bodies[j].Position - Bodies[i].Position;
			double R2 = Delta.SizeSquared();

			if (CutoffSq > 0.0 && R2 > CutoffSq)
			{
				continue;
			}

			// 软化：给距离加下限，彻底消除 r→0 的奇点，否则擦肩而过时数值溢出。
			const double Soft = Bodies[i].SofteningOverride > 0.0
				? Bodies[i].SofteningOverride
				: SofteningLength;
			R2 += Soft * Soft;

			// 1/r³
			const double InvR = 1.0 / FMath::Sqrt(R2);
			const double InvR3 = InvR * InvR * InvR;

			// 用引力源质量而非 Mass：测试粒子作为源不吸引别人。
			Sum += GravitationalSourceMass(Bodies[j]) * InvR3 * Delta;
		}

		// 外力与阻尼逐子步求值（显式欧拉，dt 逐关卡派生、不可硬编码）。引力单独存一份
		// 供星图箭头读，不和外力/阻尼混，否则推力会搅混方向。
		Bodies[i].GravitationalAcceleration = G * Sum;
		Accelerations[i] = G * Sum
			+ Bodies[i].ExternalAcceleration
			- Bodies[i].LinearDamping * Bodies[i].Velocity;
	}

	for (int32 i = 0; i < N; ++i)
	{
		Bodies[i].Acceleration = Accelerations[i];
	}
}

///////////////////////////////////////////////////////////////////////////////
// 积分

void FNBodySimulator::StepVelocityVerlet(double Dt)
{
	const int32 N = Bodies.Num();
	if (N == 0)
	{
		return;
	}

	// ── 辛积分核心：位置半步 → 速度全步 → 位置半步，保持相空间体积、误差有界 ──
	for (int32 i = 0; i < N; ++i)
	{
		Bodies[i].Position += Bodies[i].Velocity * (Dt * 0.5);
	}

	ComputeAccelerations();

	for (int32 i = 0; i < N; ++i)
	{
		Bodies[i].Velocity += Bodies[i].Acceleration * Dt;
		Bodies[i].Position += Bodies[i].Velocity * (Dt * 0.5);
	}

	SimulatedTime += Dt;
}

void FNBodySimulator::StepEuler(double Dt)
{
	// 显式欧拉：一阶、误差同向累积，轨道会螺旋坠入中心天体。仅用于对比验证。
	ComputeAccelerations();

	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		Bodies[i].Velocity += Bodies[i].Acceleration * Dt;
		Bodies[i].Position += Bodies[i].Velocity * Dt;
	}

	SimulatedTime += Dt;
}

///////////////////////////////////////////////////////////////////////////////
// 接触

void FNBodySimulator::ComputeContactShares(double MassI, double MassJ,
                                           double& OutShareI, double& OutShareJ)
{
	// 「谁轻谁让路」：位移按对方质量占比分。质量 ≤ 0 是测试粒子，它让全部、对方不让。
	// **不能写成 m_j/(m_i+m_j)** —— m_i = 0 时会反成对方承担 100%，两个都 ≤ 0 时 0/0。
	// 飞船虽是测试粒子，这里仍按真实质量比让路（相对行星等于让全部）。
	const bool bRealI = MassI > 0.0;
	const bool bRealJ = MassJ > 0.0;

	if (bRealI && bRealJ)
	{
		const double Total = MassI + MassJ;
		OutShareI = MassJ / Total;
		OutShareJ = MassI / Total;
	}
	else if (bRealI)
	{
		OutShareI = 0.0;
		OutShareJ = 1.0;
	}
	else if (bRealJ)
	{
		OutShareI = 1.0;
		OutShareJ = 0.0;
	}
	else
	{
		// 两个都是测试粒子：没有质量可以分配，对半分。
		OutShareI = 0.5;
		OutShareJ = 0.5;
	}
}

namespace
{
	/** 单个接触点（求解器单位 AU / AU每年）。 */
	struct FProbeContactPoint
	{
		/** 探针侧力臂：相对探针天体中心的探针位置（AU）。 */
		FVector3d Offset = FVector3d::ZeroVector;

		/** 球心 → 接触点的单位方向。 */
		FVector3d Normal = FVector3d::ZeroVector;

		/** 接触点所在那层地表半径：球侧力臂 = Normal × SurfaceRadius。 */
		double SurfaceRadius = 0.0;

		/** 穿透深度（> 0）。 */
		double Depth = 0.0;

		/** 迭代开始前的法向相对速度；恢复系数按它判「接近」，不受迭代中途值影响。 */
		double InitialNormalVelocity = 0.0;

		/** 本点已累计的法向冲量，也就是摩擦锥的半径。 */
		double AccumulatedNormalImpulse = 0.0;

		/** 固定的切向正交基。跨迭代不变，否则切向冲量没有累计参照，摩擦锥夹不住。 */
		FVector3d TangentU = FVector3d::ZeroVector;
		FVector3d TangentV = FVector3d::ZeroVector;

		/** 累计切向冲量在 (TangentU, TangentV) 上的分量。 */
		double AccumulatedTangentU = 0.0;
		double AccumulatedTangentV = 0.0;
	};

	/**
	 * 每对天体最多解几个接触点。飞船有上百个探针，机腹平贴地面时同时穿透的会超过 16，
	 * 所以边收集边截断：满了替换最浅的那个，全程不扩容、不排序。
	 */
	constexpr int32 MaxContactPointsPerPair = 16;

	/** 顺序冲量迭代次数。一对天体、≤16 点在 4 次内已收敛到 1e-3。 */
	constexpr int32 ContactIterations = 4;

	double InverseMassOf(const FCosmosBodyState& Body)
	{
		return Body.Mass > 0.0 ? 1.0 / Body.Mass : 0.0;
	}

	/** 接触点速度：v + ω × r。 */
	FVector3d PointVelocity(const FCosmosBodyState& Body, const FVector3d& LeverArm)
	{
		return Body.Velocity + FVector3d::CrossProduct(Body.AngularVelocity, LeverArm);
	}

	/**
	 * 球侧力臂：球心 → 接触点，长度取**该探针脚下的地表半径**（地形抬高时大于球半径），
	 * 不是探针体的 −Offset —— 两者只在探针中心与球心重合时才碰巧相等。
	 */
	FVector3d SphereLeverArm(const FProbeContactPoint& Contact)
	{
		return Contact.Normal * Contact.SurfaceRadius;
	}

	/** 由法线构造固定切向正交基（确定性，不依赖状态）。 */
	void BuildTangentBasis(const FVector3d& Normal, FVector3d& OutU, FVector3d& OutV)
	{
		// 挑与法线夹角最大的世界轴当叉乘把手，避免退化。
		const FVector3d Helper = FMath::Abs(Normal.Z) < 0.9
			? FVector3d(0.0, 0.0, 1.0)
			: FVector3d(1.0, 0.0, 0.0);
		OutU = FVector3d::CrossProduct(Helper, Normal).GetSafeNormal();
		OutV = FVector3d::CrossProduct(Normal, OutU);
	}

	/**
	 * 沿 Direction 的接触有效质量：1 / (1/m₁ + 1/m₂ + 角项)。
	 * 两侧各用自己的力臂；非刚体（ApplyInverseInertia 返回零）的角项为零，
	 * 退化成经典两体冲量。
	 */
	double EffectiveMassAlong(
		const FCosmosBodyState& ProbeBody, const FCosmosBodyState& SphereBody,
		const FVector3d& ProbeOffset, const FVector3d& SphereOffset,
		const FVector3d& Direction)
	{
		double InverseMass =
			InverseMassOf(ProbeBody) + InverseMassOf(SphereBody);

		// 探针侧：r × d → I⁻¹(r × d) × r。
		const FVector3d ProbeArm = FVector3d::CrossProduct(ProbeOffset, Direction);
		InverseMass += FVector3d::DotProduct(Direction, FVector3d::CrossProduct(
			ProbeBody.ApplyInverseInertia(ProbeArm), ProbeOffset));

		// 球侧同理，力臂是球心 → 接触点。
		const FVector3d SphereArm = FVector3d::CrossProduct(SphereOffset, Direction);
		InverseMass += FVector3d::DotProduct(Direction, FVector3d::CrossProduct(
			SphereBody.ApplyInverseInertia(SphereArm), SphereOffset));

		return InverseMass > 0.0 ? 1.0 / InverseMass : 0.0;
	}

	/** 把冲量施加到两侧：探针侧 +J（力臂 ProbeOffset），球侧 −J（力臂 SphereOffset）。 */
	void ApplyContactImpulse(
		FCosmosBodyState& ProbeBody, FCosmosBodyState& SphereBody,
		const FVector3d& ProbeOffset, const FVector3d& SphereOffset,
		const FVector3d& Impulse)
	{
		if (ProbeBody.Mass > 0.0)
		{
			ProbeBody.Velocity += Impulse / ProbeBody.Mass;
		}
		ProbeBody.AngularVelocity += ProbeBody.ApplyInverseInertia(
			FVector3d::CrossProduct(ProbeOffset, Impulse));

		if (SphereBody.Mass > 0.0)
		{
			SphereBody.Velocity -= Impulse / SphereBody.Mass;
		}
		SphereBody.AngularVelocity += SphereBody.ApplyInverseInertia(
			FVector3d::CrossProduct(SphereOffset, -Impulse));
	}
}

void FNBodySimulator::ResolveContacts()
{
	const int32 N = Bodies.Num();

	for (int32 i = 0; i < N; ++i)
	{
		for (int32 j = i + 1; j < N; ++j)
		{
			const bool bProbeI = Bodies[i].ContactProbes.Num() > 0;
			const bool bProbeJ = Bodies[j].ContactProbes.Num() > 0;

			// 一方探针一方球体走探针分支；双方都带探针不存在，会退回球-球分支 —— 那时 Radius 必须由调用方给对。
			if (bProbeI != bProbeJ)
			{
				const int32 ProbeIndex = bProbeI ? i : j;
				const int32 SphereIndex = bProbeI ? j : i;
				if (Bodies[SphereIndex].Radius > 0.0)
				{
					ResolveProbeContact(ProbeIndex, SphereIndex);
				}
				// 对方是点质量：没有任何可接触的表面，跳过。
				continue;
			}

			const double ContactDistance = Bodies[i].Radius + Bodies[j].Radius;
			if (ContactDistance <= 0.0)
			{
				// 两个都是点质量（恒星与没设半径的天体都走这一支，接触项对它们无操作）。
				continue;
			}

			const FVector3d Delta = Bodies[j].Position - Bodies[i].Position;
			const double DistanceSq = Delta.SizeSquared();
			if (DistanceSq >= ContactDistance * ContactDistance)
			{
				continue;
			}

			const double Distance = FMath::Sqrt(DistanceSq);

			// 完全重合时 Delta 没有方向，任取一个轴 —— 下一次解算看到的已是真实分离方向。
			const FVector3d Normal = Distance > 1e-12
				? Delta / Distance
				: FVector3d(1.0, 0.0, 0.0);

			double ShareI = 0.0;
			double ShareJ = 0.0;
			ComputeContactShares(Bodies[i].Mass, Bodies[j].Mass, ShareI, ShareJ);

			// ① 位置：推开到刚好相切，**不留穿透余量** —— 余量会让天体每步下陷，长期看是「地表在漏」。
			const double Overlap = ContactDistance - Distance;
			Bodies[i].Position -= Normal * (Overlap * ShareI);
			Bodies[j].Position += Normal * (Overlap * ShareJ);

			// ② 速度：默认只抹相对接近的法向分量、切向留着（光滑面滑行）。抹**相对**速度
			//    而非各自的，后者会吃掉总动量。带 bZeroVelocityOnContact 的走「撞上即停」特例。
			//    飞船改走探针冲量后不再置位；仍置位的是编辑器态离线轨迹预览与测试。
			const bool bZeroI = Bodies[i].bZeroVelocityOnContact;
			const bool bZeroJ = Bodies[j].bZeroVelocityOnContact;

			// 只有一方要求清零才走特例；双方都要求是误配，退回默认法向抹除。
			if (bZeroI != bZeroJ)
			{
				// 把带开关的那一方整条速度钉到对方上（相对速度归零，随行星一起动）。
				if (bZeroI)
				{
					Bodies[i].Velocity = Bodies[j].Velocity;
				}
				else
				{
					Bodies[j].Velocity = Bodies[i].Velocity;
				}
				continue;
			}

			const double Approach = FVector3d::DotProduct(
				Bodies[j].Velocity - Bodies[i].Velocity, Normal);

			if (Approach < 0.0)
			{
				Bodies[i].Velocity += Normal * (Approach * ShareI);
				Bodies[j].Velocity -= Normal * (Approach * ShareJ);
			}
		}
	}
}

void FNBodySimulator::ResolveProbeContact(int32 ProbeIndex, int32 SphereIndex)
{
	FCosmosBodyState& ProbeBody = Bodies[ProbeIndex];
	FCosmosBodyState& SphereBody = Bodies[SphereIndex];

	const double SphereRadius = SphereBody.Radius;

	// 逐探针地表半径只在「算它的那颗球」上有效：索引或数量对不上时退回球面。
	const bool bUsePerProbe =
		ProbeBody.ContactSurfaceSphereIndex == SphereIndex
		&& ProbeBody.ContactProbeSurfaceRadii.Num() == ProbeBody.ContactProbes.Num();

	// 快速排除：中心距 ≥ 排除半径 + 最远探针时不可能接触。抬高地形时逐探针半径会大于
	// 球半径，排除半径取两者较大者，否则那条接触会被整个漏掉。
	double RejectRadius = SphereRadius;
	if (bUsePerProbe)
	{
		for (const double ProbeSurfaceRadius : ProbeBody.ContactProbeSurfaceRadii)
		{
			RejectRadius = FMath::Max(RejectRadius, ProbeSurfaceRadius);
		}
	}

	const double CenterDistance = (ProbeBody.Position - SphereBody.Position).Size();
	if (CenterDistance >= RejectRadius + ProbeBody.ContactProbeRadius)
	{
		return;
	}

	// ① 收集穿透的探针。只解最深一个的话，机身侧倾着地时接触力矩为零，船永远不会翻。
	//    边收集边截断：槽位固定，满了只有比当前最浅者更深的新点才顶掉它，零堆分配。
	TArray<FProbeContactPoint, TInlineAllocator<MaxContactPointsPerPair>> Contacts;
	Contacts.Reserve(MaxContactPointsPerPair);

	for (int32 Index = 0; Index < ProbeBody.ContactProbes.Num(); ++Index)
	{
		// 机体系 → 世界系，每子步重算：一帧几十个子步、机身还在转，沿用帧首朝向会让接触点
		// 停在上一步的船身上，位置修正按错位置推、冲量打在错力臂上，落地抖成自激振荡。
		const FVector3d Probe =
			ProbeBody.Orientation.RotateVector(ProbeBody.ContactProbes[Index]);
		const double ProbeSurfaceRadius =
			bUsePerProbe ? ProbeBody.ContactProbeSurfaceRadii[Index] : SphereRadius;

		const FVector3d Delta = ProbeBody.Position + Probe - SphereBody.Position;
		const double Distance = Delta.Size();
		const double Penetration = ProbeSurfaceRadius - Distance;

		// NaN / Inf 必须当「没有接触」跳过：`<=` 对 NaN 恒为 false，会把它当有效接触收进来，
		// 一路污染速度与位置。探针正好落在球心时法线也无定义。
		if (!FMath::IsFinite(Penetration) || !FMath::IsFinite(Distance)
			|| Penetration <= 0.0 || Distance <= 1.0e-12)
		{
			continue;
		}

		FProbeContactPoint Contact;
		Contact.Offset = Probe;
		Contact.Normal = Delta / Distance;
		Contact.SurfaceRadius = ProbeSurfaceRadius;
		Contact.Depth = Penetration;
		BuildTangentBasis(Contact.Normal, Contact.TangentU, Contact.TangentV);

		if (Contacts.Num() < MaxContactPointsPerPair)
		{
			Contacts.Add(Contact);
			continue;
		}

		// 满了：找当前最浅的槽位，新点更深才替换。
		int32 Shallowest = 0;
		for (int32 Slot = 1; Slot < Contacts.Num(); ++Slot)
		{
			if (Contacts[Slot].Depth < Contacts[Shallowest].Depth)
			{
				Shallowest = Slot;
			}
		}
		if (Contact.Depth > Contacts[Shallowest].Depth)
		{
			Contacts[Shallowest] = Contact;
		}
	}

	if (Contacts.Num() == 0)
	{
		return;
	}

	const double Restitution = FMath::Clamp(ProbeBody.ContactRestitution, 0.0, 1.0);
	const double Friction = FMath::Max(ProbeBody.ContactFriction, 0.0);

	// 迭代前的法向速度：恢复系数只对「这一步开始时正在接近」的接触生效。
	for (FProbeContactPoint& Contact : Contacts)
	{
		const FVector3d SphereOffset = SphereLeverArm(Contact);
		const FVector3d Relative =
			PointVelocity(ProbeBody, Contact.Offset)
			- PointVelocity(SphereBody, SphereOffset);
		Contact.InitialNormalVelocity = FVector3d::DotProduct(Relative, Contact.Normal);
	}

	// ② 顺序冲量：逐点、逐轮把法向相对速度压到目标值，再用库仑摩擦削切向。
	for (int32 Iteration = 0; Iteration < ContactIterations; ++Iteration)
	{
		for (FProbeContactPoint& Contact : Contacts)
		{
			const FVector3d SphereOffset = SphereLeverArm(Contact);

			const FVector3d RelativeBefore =
				PointVelocity(ProbeBody, Contact.Offset)
				- PointVelocity(SphereBody, SphereOffset);

			const double NormalVelocity =
				FVector3d::DotProduct(RelativeBefore, Contact.Normal);
			const double TargetNormalVelocity = -Restitution * Contact.InitialNormalVelocity;
			const double NormalMass = EffectiveMassAlong(
				ProbeBody, SphereBody, Contact.Offset, SphereOffset, Contact.Normal);

			// 法向冲量只能推不能拉，物体不会被「粘」回去。
			if (NormalMass > 0.0)
			{
				const double DeltaLambda = (TargetNormalVelocity - NormalVelocity) * NormalMass;
				if (DeltaLambda > 0.0)
				{
					Contact.AccumulatedNormalImpulse += DeltaLambda;
					ApplyContactImpulse(ProbeBody, SphereBody, Contact.Offset, SphereOffset,
						Contact.Normal * DeltaLambda);
				}
			}

			// 摩擦用**施加法向冲量之后**的速度：法向先消掉，切向才是真正要削的。
			const FVector3d Relative =
				PointVelocity(ProbeBody, Contact.Offset)
				- PointVelocity(SphereBody, SphereOffset);
			const double NormalNow = FVector3d::DotProduct(Relative, Contact.Normal);
			const FVector3d TangentVelocity = Relative - Contact.Normal * NormalNow;

			// 切向有效质量按两根基轴分别取（对角近似，忽略面内耦合）：球面接触的惯量近似
			// 各向同性，够用又省掉 2×2 求逆。
			const double TangentMassU = EffectiveMassAlong(
				ProbeBody, SphereBody, Contact.Offset, SphereOffset, Contact.TangentU);
			const double TangentMassV = EffectiveMassAlong(
				ProbeBody, SphereBody, Contact.Offset, SphereOffset, Contact.TangentV);

			if (Friction > 0.0 && TangentMassU > 0.0 && TangentMassV > 0.0)
			{
				// 候选累计 = 上一轮累计 + 「把当前切向速度压到零」所需的增量。
				double CandidateU = Contact.AccumulatedTangentU
					- FVector3d::DotProduct(TangentVelocity, Contact.TangentU) * TangentMassU;
				double CandidateV = Contact.AccumulatedTangentV
					- FVector3d::DotProduct(TangentVelocity, Contact.TangentV) * TangentMassV;

				// 库仑锥夹**累计**而非单轮增量：增量式在 4 轮里能累计到 4×μλ_n，等效 μ≈2.4，
				// 落地会「发粘」。写错的表现是切向滑移过度（μ 显得偏小），不是发粘。
				const double MaxFriction = Friction * Contact.AccumulatedNormalImpulse;
				const double CandidateSize =
					FMath::Sqrt(CandidateU * CandidateU + CandidateV * CandidateV);
				if (CandidateSize > MaxFriction)
				{
					const double Scale = CandidateSize > 1.0e-30 ? MaxFriction / CandidateSize : 0.0;
					CandidateU *= Scale;
					CandidateV *= Scale;
				}

				// 只施加与上一轮的差值，累计量本身才是状态。
				const double DeltaU = CandidateU - Contact.AccumulatedTangentU;
				const double DeltaV = CandidateV - Contact.AccumulatedTangentV;
				Contact.AccumulatedTangentU = CandidateU;
				Contact.AccumulatedTangentV = CandidateV;

				if (DeltaU != 0.0 || DeltaV != 0.0)
				{
					ApplyContactImpulse(ProbeBody, SphereBody, Contact.Offset, SphereOffset,
						Contact.TangentU * DeltaU + Contact.TangentV * DeltaV);
				}
			}
		}
	}

	// ③ 位置修正：只推一次，方向取所有法线按穿透量加权的平均。逐点各推会和速度冲量
	//    叠加成「越推越远」；直接用最深点的法线则会在侧倾着地时把重心慢慢推偏，
	//    几次之后整机横着滑出球面、接触全部消失。
	const FProbeContactPoint* Deepest = &Contacts[0];
	double DeepestDepth = 0.0;
	double DepthSum = 0.0;
	FVector3d WeightedNormalSum = FVector3d::ZeroVector;

	for (const FProbeContactPoint& Contact : Contacts)
	{
		DeepestDepth = FMath::Max(DeepestDepth, Contact.Depth);
		DepthSum += Contact.Depth;
		WeightedNormalSum += Contact.Normal * Contact.Depth;

		if (Contact.Depth > Deepest->Depth)
		{
			Deepest = &Contact;
		}
	}

	// 方向可信度 = |Σdᵢnᵢ| / Σdᵢ。低于 0.25 说明接触点把球心围住、方向由几何噪声主导，
	// 此退化成最深点的法线（GetSafeNormal 此时仍给单位向量，IsZero 抓不住）。
	FVector3d PushNormal = WeightedNormalSum.GetSafeNormal();
	if (PushNormal.IsZero() || WeightedNormalSum.Size() < 0.25 * DepthSum)
	{
		PushNormal = Deepest->Normal;
	}

	double ShareProbe = 0.0;
	double ShareSphere = 0.0;
	ComputeContactShares(ProbeBody.Mass, SphereBody.Mass, ShareProbe, ShareSphere);

	ProbeBody.Position += PushNormal * (DeepestDepth * ShareProbe);
	SphereBody.Position -= PushNormal * (DeepestDepth * ShareSphere);
}

void FNBodySimulator::StepExact(double TimeStep)
{
	switch (Integrator)
	{
	case ECosmosIntegrator::VelocityVerlet:
		StepVelocityVerlet(TimeStep);
		break;

	case ECosmosIntegrator::Euler:
		StepEuler(TimeStep);
		break;

	case ECosmosIntegrator::None:
		// 不积分：天体保持初始速度做匀速直线运动
		SimulatedTime += TimeStep;
		break;
	}

	// 姿态先于接触积分：接触冲量要读**本步**的角速度与朝向来算力臂，再把力矩写回同一个 ω。
	IntegrateRigidBodyOrientations(TimeStep);

	// 约束放在积分之后：三种积分器与预测副本共用同一处接触行为。位置被钳过后，缓存里的
	// Acceleration 对应钳制前的位置 —— 这是 Velocity Verlet 固有的滞后，不是接触引入的。
	ResolveContacts();
}

void FNBodySimulator::Step(float Dt)
{
	if (Dt <= 0.0f)
	{
		return;
	}

	Accumulator += Dt;
	StepExact(Accumulator);
	Accumulator = 0.0;
}

///////////////////////////////////////////////////////////////////////////////
// 诊断

double FNBodySimulator::GetTotalEnergy() const
{
	const double G = GetG();

	double Kinetic = 0.0;
	double Potential = 0.0;

	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		Kinetic += 0.5 * Bodies[i].Mass * Bodies[i].Velocity.SizeSquared();
	}

	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		for (int32 j = i + 1; j < Bodies.Num(); ++j)
		{
			double R2 = (Bodies[j].Position - Bodies[i].Position).SizeSquared();
			const double Soft = Bodies[i].SofteningOverride > 0.0
				? Bodies[i].SofteningOverride
				: SofteningLength;
			R2 += Soft * Soft;

			// 势能也用引力源质量，与 ComputeAccelerations 同口径，否则测试粒子会贡献不存在的势能。
			Potential -= G * GravitationalSourceMass(Bodies[i])
				* GravitationalSourceMass(Bodies[j]) / FMath::Sqrt(R2);
		}
	}

	return Kinetic + Potential;
}

FVector3d FNBodySimulator::GetTotalMomentum() const
{
	FVector3d Sum = FVector3d::ZeroVector;
	for (const FCosmosBodyState& B : Bodies)
	{
		Sum += B.Mass * B.Velocity;
	}
	return Sum;
}

FVector3d FNBodySimulator::GetTotalAngularMomentum() const
{
	// 先算质心，角动量必须相对质心才有意义
	FVector3d CenterOfMass = FVector3d::ZeroVector;
	double TotalMass = 0.0;
	for (const FCosmosBodyState& B : Bodies)
	{
		CenterOfMass += B.Mass * B.Position;
		TotalMass += B.Mass;
	}
	if (TotalMass <= 0.0)
	{
		return FVector3d::ZeroVector;
	}
	CenterOfMass /= TotalMass;

	FVector3d Sum = FVector3d::ZeroVector;
	for (const FCosmosBodyState& B : Bodies)
	{
		Sum += B.Mass * FVector3d::CrossProduct(B.Position - CenterOfMass, B.Velocity);
	}
	return Sum;
}

void FNBodySimulator::MarkEnergyBaseline()
{
	EnergyBaseline = GetTotalEnergy();
	bEnergyBaselineValid = true;
}

double FNBodySimulator::GetEnergyDriftRatio() const
{
	if (!bEnergyBaselineValid || FMath::IsNearlyEqual(EnergyBaseline, 0.0))
	{
		return 0.0;
	}
	return (GetTotalEnergy() - EnergyBaseline) / FMath::Abs(EnergyBaseline);
}

///////////////////////////////////////////////////////////////////////////////
// 坐标转换

FVector FNBodySimulator::ToWorldLocation(const FVector3d& GameUnitPosition) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector(GameUnitPosition) * CentimetersPerAu;
	}
	return FVector(GameUnitPosition);
}

FVector3d FNBodySimulator::ToGameUnitPosition(const FVector& WorldLocation) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector3d(WorldLocation) / CentimetersPerAu;
	}
	return FVector3d(WorldLocation);
}

// ── 以下三个换算目前无人调用，改动时别以为有调用方在依赖 ──
// 它们是**实时**换算（1 AU/年 ≈ 0.032 厘米/秒），游戏时钟下还要乘 YearToSecond · TimeScale
// ≈ 1.05e5（用 1/TimeScale 是错的）。
FVector FNBodySimulator::ToWorldVelocity(const FVector3d& GameUnitVelocity) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector(GameUnitVelocity) * (CentimetersPerAu / CosmosUnits::YearToSecond);
	}
	return FVector(GameUnitVelocity);
}

double FNBodySimulator::ToWorldSeconds(double GameUnitYears) const
{
	return (UnitSystem == ECosmosUnitSystem::Game) ? GameUnitYears * CosmosUnits::YearToSecond : GameUnitYears;
}

double FNBodySimulator::ToWorldCentimeter(double GameUnitAu) const
{
	return (UnitSystem == ECosmosUnitSystem::Game) ? GameUnitAu * CentimetersPerAu : GameUnitAu;
}

///////////////////////////////////////////////////////////////////////////////
// 预测 —— 只读

double FNBodySimulator::PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G)
{
	const double Separation = (B.Position - A.Position).Size();
	const double Mu = G * (A.Mass + B.Mass);

	// 间距或总质量为零时这一对没有意义，返回 0 让调用方跳过。
	if (Separation <= 0.0 || Mu <= 0.0)
	{
		return 0.0;
	}

	return 2.0 * PI * FMath::Sqrt(Separation * Separation * Separation / Mu);
}

double FNBodySimulator::EstimateOrbitalPeriod(int32 BodyIndex) const
{
	if (!Bodies.IsValidIndex(BodyIndex))
	{
		return 0.0;
	}

	const double G = GetG();
	double Shortest = 0.0;

	for (int32 j = 0; j < Bodies.Num(); ++j)
	{
		if (j == BodyIndex)
		{
			continue;
		}

		const double Period = PairOrbitalPeriod(Bodies[BodyIndex], Bodies[j], G);
		if (Period > 0.0 && (Shortest <= 0.0 || Period < Shortest))
		{
			Shortest = Period;
		}
	}

	return Shortest;
}

bool FNBodySimulator::PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples,
                                        TArray<FVector3d>& OutPositions) const
{
	OutPositions.Reset();

	if (!Bodies.IsValidIndex(BodyIndex) || Horizon <= 0.0 || NumSamples < 2)
	{
		return false;
	}

	// 值类型，复制即得一份完整状态（见头文件注释）。
	FNBodySimulator Copy = *this;

	// 副本会继承外力与阻尼，不清零预测线就成了「一直加速」的曲线。这里预测松手后的纯引力轨迹。
	for (FCosmosBodyState& Body : Copy.Bodies)
	{
		Body.ExternalAcceleration = FVector3d::ZeroVector;
		Body.LinearDamping = 0.0;

		// 角方向的输入力矩与角阻尼同样是「松手就没了」，一并清零。
		Body.ExternalAngularAcceleration = FVector3d::ZeroVector;
		Body.AngularDamping = 0.0;
	}

	// 步长取 Horizon / (NumSamples - 1)，末点恰好落在 Horizon 上。局部量不能叫 Step：
	// 遮蔽成员函数，UE 5.8 把 C4458 当硬错误。
	const double StepSize = Horizon / double(NumSamples - 1);

	OutPositions.Reserve(NumSamples);
	OutPositions.Add(Copy.GetBody(BodyIndex).Position);

	for (int32 SampleIndex = 1; SampleIndex < NumSamples; ++SampleIndex)
	{
		Copy.StepExact(StepSize);
		OutPositions.Add(Copy.GetBody(BodyIndex).Position);
	}

	return true;
}

bool FNBodySimulator::PredictClosedOrbit(int32 BodyIndex, int32 NumSamplesPerOrbit,
                                         FCosmosClosedOrbitPrediction& Out) const
{
	Out.RelativePositions.Reset();
	Out.PrimaryPosition = FVector3d::ZeroVector;
	Out.PeriodYears = 0.0;

	if (!Bodies.IsValidIndex(BodyIndex) || NumSamplesPerOrbit < 2)
	{
		return false;
	}

	const FCosmosBodyState& Body = Bodies[BodyIndex];

	// ── 主导天体：引力 M/r² 最大，且必须比本天体重 ──
	// 「更重」不是形式主义：否则重天体绕轻天体会画出一个与实际运动无关的巨环。
	// 用 GravitationalSourceMass：测试粒子不该因质量大就成为别人的主导天体。
	int32 PrimaryIndex = INDEX_NONE;
	double BestPull = 0.0;
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		if (Index == BodyIndex)
		{
			continue;
		}

		const FCosmosBodyState& Other = Bodies[Index];
		const double SourceMass = GravitationalSourceMass(Other);
		if (!(SourceMass > 0.0) || !(SourceMass > Body.Mass))
		{
			continue;
		}

		const double Distance = (Other.Position - Body.Position).Size();
		if (!(Distance > 0.0) || !FMath::IsFinite(Distance))
		{
			continue;
		}

		const double Pull = SourceMass / (Distance * Distance);
		if (Pull > BestPull)
		{
			BestPull = Pull;
			PrimaryIndex = Index;
		}
	}

	if (PrimaryIndex == INDEX_NONE)
	{
		return false;
	}

	const FCosmosBodyState& Primary = Bodies[PrimaryIndex];
	const FVector3d RelativePosition = Body.Position - Primary.Position;
	const FVector3d RelativeVelocity = Body.Velocity - Primary.Velocity;
	const double Distance = RelativePosition.Size();
	const double SpeedSquared = RelativeVelocity.SizeSquared();
	const double Mu = GetG() * (Body.Mass + Primary.Mass);

	if (!(Distance > 0.0) || !(Mu > 0.0)
		|| !FMath::IsFinite(Distance) || !FMath::IsFinite(SpeedSquared) || !FMath::IsFinite(Mu))
	{
		return false;
	}

	// ── 密切轨道：ε < 0 才是椭圆；ε ≥ 0 是抛物线/双曲线，没有「一圈」可闭合 ──
	const double SpecificEnergy = 0.5 * SpeedSquared - Mu / Distance;
	if (!(SpecificEnergy < 0.0) || !FMath::IsFinite(SpecificEnergy))
	{
		return false;
	}

	const double SemiMajorAxis = -Mu / (2.0 * SpecificEnergy);
	if (!(SemiMajorAxis > 0.0) || !FMath::IsFinite(SemiMajorAxis))
	{
		return false;
	}

	// 偏心率。e≈1 是径向/抛物退化（如贴着行星静止的飞船）：会「闭合」成穿过主天体的直线。
	const FVector3d EccentricityVector =
		((SpeedSquared - Mu / Distance) * RelativePosition
			- FVector3d::DotProduct(RelativePosition, RelativeVelocity) * RelativeVelocity) / Mu;
	const double Eccentricity = EccentricityVector.Size();
	if (!(Eccentricity < 0.99) || !FMath::IsFinite(Eccentricity))
	{
		return false;
	}

	const double Period = 2.0 * PI * FMath::Sqrt(
		SemiMajorAxis * SemiMajorAxis * SemiMajorAxis / Mu);
	if (!(Period > 0.0) || !FMath::IsFinite(Period))
	{
		return false;
	}

	// 与 PredictTrajectory 同口径：复制求解器，清零外力/阻尼，只算松手后的纯引力。
	FNBodySimulator Copy = *this;
	for (FCosmosBodyState& CopyBody : Copy.Bodies)
	{
		CopyBody.ExternalAcceleration = FVector3d::ZeroVector;
		CopyBody.LinearDamping = 0.0;

		// 角方向的输入力矩与角阻尼同样是「松手就没了」，一并清零。
		CopyBody.ExternalAngularAcceleration = FVector3d::ZeroVector;
		CopyBody.AngularDamping = 0.0;
	}

	// ── 用「转满一圈后离起点最近」找闭合时刻，而不是照密切周期截断 ──
	// 密切周期只是 t=0 的局部线性化，多体扰动下差数倍，按它截断会让预测线断成半截。
	// 它只用来定步长；最多积 3 个周期，扫满 2π 后取离起点最近的采样点。只认一圈之后，
	// 否则圆轨道相邻采样点间距就可能小于容差，出发不久即误判。
	const double StepSize = Period / double(NumSamplesPerOrbit - 1);
	const int32 SearchSamples = NumSamplesPerOrbit * 3;
	const double ReturnTolerance = 0.05 * SemiMajorAxis;
	constexpr double TwoPi = 2.0 * PI;
	// 留一点角度余量：最近点可能落在「跨过 2π」那一采样点之前半个步长。
	constexpr double AngleMargin = 0.1;

	TArray<FVector3d> Samples;
	Samples.Reserve(SearchSamples);

	const FVector3d StartRelative =
		Copy.GetBody(BodyIndex).Position - Copy.GetBody(PrimaryIndex).Position;
	Samples.Add(StartRelative);

	FVector3d PreviousRelative = StartRelative;
	double SweptAngle = 0.0;
	int32 ReturnSample = INDEX_NONE;
	double BestReturnDistance = TNumericLimits<double>::Max();

	for (int32 SampleIndex = 1; SampleIndex < SearchSamples; ++SampleIndex)
	{
		Copy.StepExact(StepSize);

		const FVector3d Relative =
			Copy.GetBody(BodyIndex).Position - Copy.GetBody(PrimaryIndex).Position;
		Samples.Add(Relative);

		// 步长很小，atan2(|a×b|, a·b) 就是这一步扫过的夹角（无符号，顺逆行都成立）。
		SweptAngle += FMath::Atan2(
			FVector3d::CrossProduct(PreviousRelative, Relative).Size(),
			FVector3d::DotProduct(PreviousRelative, Relative));
		PreviousRelative = Relative;

		if (SweptAngle >= TwoPi - AngleMargin)
		{
			const double ReturnDistance = (Relative - StartRelative).Size();
			if (ReturnDistance < BestReturnDistance)
			{
				BestReturnDistance = ReturnDistance;
				ReturnSample = SampleIndex;
			}
		}
	}

	// 转满一圈后都回不到起点附近：不是闭环（被拽走 / 严重进动）。
	if (ReturnSample == INDEX_NONE || BestReturnDistance > ReturnTolerance)
	{
		return false;
	}

	Out.RelativePositions.Reserve(ReturnSample + 2);
	Out.RelativePositions.Append(Samples.GetData(), ReturnSample + 1);
	Out.PrimaryPosition = Primary.Position;
	Out.PeriodYears = double(ReturnSample) * StepSize;

	// 首尾逐位相接。必须先拷到局部变量 —— 直接 Add(Out.RelativePositions[0]) 会触发容器别名校验断言。
	const FVector3d FirstPosition = Out.RelativePositions[0];
	Out.RelativePositions.Add(FirstPosition);
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// 刚体姿态

FVector3d FCosmosBodyState::InverseInertiaFromBox(double Mass, const FVector3d& HalfExtent)
{
	const FVector3d Edge = HalfExtent * 2.0;
	if (Mass <= 0.0 || Edge.X <= 0.0 || Edge.Y <= 0.0 || Edge.Z <= 0.0)
	{
		return FVector3d::ZeroVector;
	}

	const double TwelveOverMass = 12.0 / Mass;
	return FVector3d(
		TwelveOverMass / (Edge.Y * Edge.Y + Edge.Z * Edge.Z),
		TwelveOverMass / (Edge.X * Edge.X + Edge.Z * Edge.Z),
		TwelveOverMass / (Edge.X * Edge.X + Edge.Y * Edge.Y));
}

FVector3d FCosmosBodyState::InverseInertiaFromSphere(double Mass, double Radius)
{
	const double Inertia = 0.4 * Mass * Radius * Radius;
	return Inertia > 0.0 ? FVector3d(1.0 / Inertia) : FVector3d::ZeroVector;
}

void FNBodySimulator::IntegrateRigidBodyOrientations(double TimeStep)
{
	if (TimeStep <= 0.0)
	{
		return;
	}

	for (FCosmosBodyState& Body : Bodies)
	{
		if (!Body.bRigidBody)
		{
			continue;
		}

		// 角速度用显式欧拉：线速度那边上 Verlet 是因为有引力这个平滑加速度场，
		// 角加速度只有外力矩与接触冲量，且一帧最多 200 个子步，一阶足够。
		Body.AngularVelocity += Body.ExternalAngularAcceleration * TimeStep;

		// 乘法形式，k·dt ≥ 1 时直接归零。
		if (Body.AngularDamping > 0.0)
		{
			const double Decay = 1.0 - Body.AngularDamping * TimeStep;
			Body.AngularVelocity *= Decay > 0.0 ? Decay : 0.0;
		}

		// dq = exp(½·ω·dt) ⊗ q，世界系角速度 → 左乘。每步归一化，否则几千步后
		// Actor 的变换矩阵会慢慢非正交，表现为缓慢缩放。
		const double Speed = Body.AngularVelocity.Size();
		if (Speed > 0.0)
		{
			const FVector3d Axis = Body.AngularVelocity / Speed;
			const FQuat4d Delta(Axis, Speed * TimeStep);
			Body.Orientation = (Delta * Body.Orientation).GetNormalized();
		}
	}
}
