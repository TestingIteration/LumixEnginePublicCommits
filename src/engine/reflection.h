#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/core.h"
#include "core/color.h"
#include "core/delegate_list.h"
#include "core/hash.h"
#include "core/math.h"
#include "core/metaprogramming.h"
#include "core/string.h"

#include "engine/component_uid.h"
#include "engine/resource.h"

namespace Lumix
{

struct Path;
struct InputMemoryStream;
struct OutputMemoryStream;

namespace reflection
{


struct IAttribute {
	enum Type : u32 {
		MIN,
		CLAMP,
		RADIANS,
		COLOR,
		RESOURCE,
		ENUM,
		MULTILINE,
		STRING_ENUM,
		NO_UI,
	};

	virtual ~IAttribute() {}
	virtual Type getType() const = 0;
};

// we don't use method pointers here because VS has sizeof issues if IModule is forward declared
using CreateComponent = void (*)(IModule*, EntityRef);
using DestroyComponent = void (*)(IModule*, EntityRef);

struct RegisteredComponent {
	RuntimeHash name_hash;
	RuntimeHash module_hash;
	struct ComponentBase* cmp = nullptr;
};

LUMIX_ENGINE_API const ComponentBase* getComponent(ComponentType cmp_type);
LUMIX_ENGINE_API const struct PropertyBase* getProperty(ComponentType cmp_type, StringView prop);
LUMIX_ENGINE_API Span<const RegisteredComponent> getComponents();

LUMIX_ENGINE_API const PropertyBase* getPropertyFromHash(StableHash hash);
LUMIX_ENGINE_API StableHash getPropertyHash(ComponentType cmp, const char* property_name);
LUMIX_ENGINE_API bool componentTypeExists(const char* id);
LUMIX_ENGINE_API ComponentType getComponentType(StringView id);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(RuntimeHash hash);

struct ResourceAttribute : IAttribute
{
	ResourceAttribute(ResourceType type) : resource_type(type) {}
	ResourceAttribute() {}

	Type getType() const override { return RESOURCE; }

	ResourceType resource_type;
};

struct MinAttribute : IAttribute
{
	explicit MinAttribute(float min) { this->min = min; }
	MinAttribute() {}

	Type getType() const override { return MIN; }

	float min;
};

struct ClampAttribute : IAttribute
{
	ClampAttribute(float min, float max) { this->min = min; this->max = max; }
	ClampAttribute() {}

	Type getType() const override { return CLAMP; }

	float min;
	float max;
};

struct ColorAttribute : IAttribute {
	Type getType() const override { return COLOR; }
};

struct EnumAttribute : IAttribute {
	virtual u32 count(ComponentUID cmp) const = 0;
	virtual const char* name(ComponentUID cmp, u32 idx) const = 0;
	
	Type getType() const override { return ENUM; }
};

struct StringEnumAttribute : IAttribute {
	virtual u32 count(ComponentUID cmp) const = 0;
	virtual const char* name(ComponentUID cmp, u32 idx) const = 0;
	
	Type getType() const override { return STRING_ENUM; }
};


struct LUMIX_ENGINE_API PropertyBase {
	PropertyBase(IAllocator& allocator) : attributes(allocator) {}
	virtual ~PropertyBase() {}
	Array<IAttribute*> attributes;

	virtual void visit(struct IPropertyVisitor& visitor) const = 0;
	const char* name;
	ComponentBase* cmp;
};


template <typename T>
struct Property : PropertyBase {
	Property(IAllocator& allocator) : PropertyBase(allocator) {}

	using Setter = void (*)(IModule*, EntityRef, u32, const T&);
	using Getter = T (*)(IModule*, EntityRef, u32);

	void visit(IPropertyVisitor& visitor) const override;

	virtual T get(ComponentUID cmp, u32 idx) const {
		return getter(cmp.module, (EntityRef)cmp.entity, idx);
	}

	virtual void set(ComponentUID cmp, u32 idx, T val) const {
		setter(cmp.module, (EntityRef)cmp.entity, idx, val);
	}

	virtual bool isReadonly() const { return setter == nullptr; }

	Setter setter = nullptr;
	Getter getter = nullptr;
};

struct IPropertyVisitor {
	virtual ~IPropertyVisitor() {}
	virtual void visit(const Property<float>& prop) = 0;
	virtual void visit(const Property<int>& prop) = 0;
	virtual void visit(const Property<u32>& prop) = 0;
	virtual void visit(const Property<EntityPtr>& prop) = 0;
	virtual void visit(const Property<Vec2>& prop) = 0;
	virtual void visit(const Property<Vec3>& prop) = 0;
	virtual void visit(const Property<IVec3>& prop) = 0;
	virtual void visit(const Property<Vec4>& prop) = 0;
	virtual void visit(const Property<Path>& prop) = 0;
	virtual void visit(const Property<bool>& prop) = 0;
	virtual void visit(const Property<const char*>& prop) = 0;
	virtual void visit(const struct ArrayProperty& prop) = 0;
	virtual void visit(const struct BlobProperty& prop) = 0;
};


template <typename T>
void Property<T>::visit(IPropertyVisitor& visitor) const {
	visitor.visit(*this);
}


struct IEmptyPropertyVisitor : IPropertyVisitor {
	virtual ~IEmptyPropertyVisitor() {}
	void visit(const Property<float>& prop) override {}
	void visit(const Property<int>& prop) override {}
	void visit(const Property<u32>& prop) override {}
	void visit(const Property<EntityPtr>& prop) override {}
	void visit(const Property<Vec2>& prop) override {}
	void visit(const Property<Vec3>& prop) override {}
	void visit(const Property<IVec3>& prop) override {}
	void visit(const Property<Vec4>& prop) override {}
	void visit(const Property<Path>& prop) override {}
	void visit(const Property<bool>& prop) override {}
	void visit(const Property<const char*>& prop) override {}
	void visit(const ArrayProperty& prop) override {}
	void visit(const BlobProperty& prop) override {}
};

struct LUMIX_ENGINE_API ArrayProperty : PropertyBase {
	typedef u32 (*Counter)(IModule*, EntityRef);
	typedef void (*Adder)(IModule*, EntityRef, u32);
	typedef void (*Remover)(IModule*, EntityRef, u32);

	ArrayProperty(IAllocator& allocator);

	u32 getCount(ComponentUID cmp) const;
	void addItem(ComponentUID cmp, u32 idx) const;
	void removeItem(ComponentUID cmp, u32 idx) const;

	void visit(struct IPropertyVisitor& visitor) const override;
	void visitChildren(struct IPropertyVisitor& visitor) const;

	Array<PropertyBase*> children;
	Counter counter;
	Adder adder;
	Remover remover;
};

struct LUMIX_ENGINE_API BlobProperty : PropertyBase {
	BlobProperty(IAllocator& allocator);

	void visit(struct IPropertyVisitor& visitor) const override;
	void getValue(ComponentUID cmp, u32 idx, OutputMemoryStream& stream) const;
	void setValue(ComponentUID cmp, u32 idx, InputMemoryStream& stream) const;

	typedef void (*Getter)(IModule*, EntityRef, u32, OutputMemoryStream&);
	typedef void (*Setter)(IModule*, EntityRef, u32, InputMemoryStream&);

	Getter getter;
	Setter setter;
};

struct Icon { const char* name; };
inline Icon icon(const char* name) { return {name}; }

namespace detail {

#if defined __clang__ && defined _WIN32
	static const u32 FRONT_SIZE = (u32)sizeof("static StringView Lumix::reflection::detail::GetTypeNameHelper<") - 1u;
#else
	static const u32 FRONT_SIZE = sizeof("Lumix::reflection::detail::GetTypeNameHelper<") - 1u;
	static const u32 BACK_SIZE = sizeof(">::GetTypeName") - 1u;
#endif

template <typename T>
struct GetTypeNameHelper
{
	static StringView GetTypeName()
	{
		#if defined __clang__ && defined _WIN32
			const char* fn = __PRETTY_FUNCTION__;
			static const char* end = strstr(fn, ">::GetTypeName() [T = ");
			return StringView(fn + FRONT_SIZE, end);
		#elif defined(_MSC_VER) && !defined(__clang__)
			static const size_t size = sizeof(__FUNCTION__) - FRONT_SIZE - BACK_SIZE;
			return StringView(__FUNCTION__ + FRONT_SIZE, size - 1);
		#else
			static const size_t size = sizeof(__PRETTY_FUNCTION__) - FRONT_SIZE - BACK_SIZE;
			return StringView(__PRETTY_FUNCTION__ + FRONT_SIZE, size - 1);
		#endif
	}
};

LUMIX_ENGINE_API StringView normalizeTypeName(StringView type_name);

} // namespace detail


template <typename T>
const IAttribute* getAttribute(const Property<T>& prop, IAttribute::Type type) {
	for (const IAttribute* attr : prop.attributes) {
		if (attr->getType() == type) return attr;
	}
	return nullptr;
}

template <typename T>
StringView getTypeName()
{
	return detail::normalizeTypeName(detail::GetTypeNameHelper<T>::GetTypeName());
}

struct Variant {
	Variant() { type = I32; i = 0; }
	enum Type : u32 {
		VOID,
		PTR,
		BOOL,
		I32,
		U32,
		FLOAT,
		CSTR,
		ENTITY,
		VEC2,
		VEC3,
		VEC4,
		DVEC3,
		COLOR,
		QUAT
	} type;
	union {
		bool b;
		i32 i;
		u32 u;
		float f;
		const char* s;
		EntityPtr e;
		Vec2 v2;
		Vec3 v3;
		Vec4 v4;
		DVec3 dv3;
		void* ptr;
		Color color;
		Quat quat;
	};

	void operator =(bool v) { b = v; type = BOOL; }
	void operator =(i32 v) { i = v; type = I32; }
	void operator =(u32 v) { u = v; type = U32; }
	void operator =(float v) { f = v; type = FLOAT; }
	void operator =(const Path& v) { s = v.c_str(); type = CSTR; }
	void operator =(const char* v) { s = v; type = CSTR; }
	void operator =(EntityPtr v) { e = v; type = ENTITY; }
	void operator =(Vec2 v) { v2 = v; type = VEC2; }
	void operator =(Vec3 v) { v3 = v; type = VEC3; }
	void operator =(Vec4 v) { v4 = v; type = VEC4; }
	void operator =(const DVec3& v) { dv3 = v; type = DVEC3; }
	void operator =(void* v) { ptr = v; type = PTR; }
	void operator =(Color c) { color = c; type = COLOR; }
	void operator =(const Quat& q) { quat = q; type = QUAT; }
};

struct TypeDescriptor {
	Variant::Type type;
	StringView type_name;
	bool is_const;
	bool is_reference;
	bool is_pointer;
	u32 size;
};

template <typename T> struct VariantTag {};

template <typename T> inline Variant::Type _getVariantType(VariantTag<T*>) { return Variant::PTR; }
template <typename T> inline Variant::Type _getVariantType(VariantTag<T>) { return Variant::PTR; }
inline Variant::Type _getVariantType(VariantTag<void>) { return Variant::VOID; }
inline Variant::Type _getVariantType(VariantTag<bool>) { return Variant::BOOL; }
inline Variant::Type _getVariantType(VariantTag<i32>) { return Variant::I32; }
inline Variant::Type _getVariantType(VariantTag<u32>) { return Variant::U32; }
inline Variant::Type _getVariantType(VariantTag<float>) { return Variant::FLOAT; }
inline Variant::Type _getVariantType(VariantTag<const char*>) { return Variant::CSTR; }
inline Variant::Type _getVariantType(VariantTag<EntityPtr>) { return Variant::ENTITY; }
inline Variant::Type _getVariantType(VariantTag<EntityRef>) { return Variant::ENTITY; }
inline Variant::Type _getVariantType(VariantTag<Vec2>) { return Variant::VEC2; }
inline Variant::Type _getVariantType(VariantTag<Vec3>) { return Variant::VEC3; }
inline Variant::Type _getVariantType(VariantTag<Vec4>) { return Variant::VEC4; }
inline Variant::Type _getVariantType(VariantTag<Path>) { return Variant::CSTR; }
inline Variant::Type _getVariantType(VariantTag<Color>) { return Variant::COLOR; }
inline Variant::Type _getVariantType(VariantTag<DVec3>) { return Variant::DVEC3; }
inline Variant::Type _getVariantType(VariantTag<Quat>) { return Variant::QUAT; }
template <typename T> inline Variant::Type getVariantType() { return _getVariantType(VariantTag<RemoveCVR<T>>{}); }

template <typename T> TypeDescriptor toTypeDescriptor() {
	TypeDescriptor td;
	td.type_name = getTypeName<RemoveCVR<RemovePointer<T>>>();
	td.type = getVariantType<T>();
	td.is_const = !IsSame<T, typename RemoveConst<T>::Type>::Value;
	td.is_reference = !IsSame<T, typename RemoveReference<T>::Type>::Value;
	td.is_pointer = !IsSame<T, RemovePointer<T>>::Value;
	if constexpr (IsSame<T, void>::Value) {
		td.size = 0;
	}
	else {
		td.size = sizeof(T);
	}
	return td;
}

struct FunctionBase {
	using DummyFnType = void (*)();

	virtual ~FunctionBase() {}

	virtual u32 getArgCount() const = 0;
	virtual TypeDescriptor getReturnType() const = 0;
	virtual TypeDescriptor getArgType(int i) const = 0;
	virtual void invoke(void* obj, Span<u8> ret_mem, Span<const Variant> args) const = 0;
	// we can use this in Delegate::bindRaw, so there's less overhead
	virtual DummyFnType getDelegateStub() = 0;

	const char* name;
};

struct EventBase {
	struct Callback {
		virtual ~Callback() {}
		virtual void invoke(Span<const Variant> args) = 0;
	};

	virtual ~EventBase() {}
	virtual u32 getArgCount() const = 0;
	virtual TypeDescriptor getArgType(int i) const = 0;
	virtual void bind(void* object, Callback* callback) const = 0;
	[[nodiscard]] virtual bool bind(void* object, void* fn_object, FunctionBase* function) const = 0;

	const char* name;
};

inline bool fromVariant(int i, Span<const Variant> args, VariantTag<bool>) { return args[i].b; }
inline float fromVariant(int i, Span<const Variant> args, VariantTag<float>) { return args[i].f; }
inline const char* fromVariant(int i, Span<const Variant> args, VariantTag<const char*>) { return args[i].s; }
inline Path fromVariant(int i, Span<const Variant> args, VariantTag<Path>) { return Path(args[i].s); }
inline i32 fromVariant(int i, Span<const Variant> args, VariantTag<i32>) { return args[i].i; }
inline u32 fromVariant(int i, Span<const Variant> args, VariantTag<u32>) { return args[i].u; }
inline Color fromVariant(int i, Span<const Variant> args, VariantTag<Color>) { return args[i].color; }
inline Vec2 fromVariant(int i, Span<const Variant> args, VariantTag<Vec2>) { return args[i].v2; }
inline Vec3 fromVariant(int i, Span<const Variant> args, VariantTag<Vec3>) { return args[i].v3; }
inline Vec4 fromVariant(int i, Span<const Variant> args, VariantTag<Vec4>) { return args[i].v4; }
inline Quat fromVariant(int i, Span<const Variant> args, VariantTag<Quat>) { return args[i].quat; }
inline DVec3 fromVariant(int i, Span<const Variant> args, VariantTag<DVec3>) { return args[i].dv3; }
inline EntityPtr fromVariant(int i, Span<const Variant> args, VariantTag<EntityPtr>) { return args[i].e; }
inline EntityRef fromVariant(int i, Span<const Variant> args, VariantTag<EntityRef>) { return (EntityRef)args[i].e; }
inline void* fromVariant(int i, Span<const Variant> args, VariantTag<void*>) { return args[i].ptr; }
template <typename T> inline T* fromVariant(int i, Span<const Variant> args, VariantTag<T*>) { return (T*)args[i].ptr; }
template <typename T> inline T& fromVariant(int i, Span<const Variant> args, VariantTag<T>) { return *(T*)args[i].ptr; }

template <typename F> struct Event;

template <typename T> struct ArgToTypeDescriptor;

template <typename R, typename C, typename... Args>
struct ArgToTypeDescriptor<R(C::*)(Args...)>
{
	static TypeDescriptor get(int i) {
		TypeDescriptor expand[] = {
			toTypeDescriptor<Args>()...,
			Variant::Type::VOID
		};
		return expand[i];
	}
};

template <typename R, typename C, typename... Args>
struct ArgToTypeDescriptor<R(C::*)(Args...) const>
{
	static TypeDescriptor get(int i) {
		TypeDescriptor expand[] = {
			toTypeDescriptor<Args>()...,
			Variant::Type::VOID
		};
		return expand[i];
	}
};

template <typename T> struct VariantCaller;

template <typename R, typename C, typename... Args>
struct VariantCaller<R (C::*)(Args...)> {
	template <int... I>
	static void call(C* inst, auto f, Span<u8> ret_mem, Span<const Variant> args, Indices<I...>& indices) {
		if constexpr (IsSame<R, void>::Value) {
			(inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
		}
		else {
			auto v = (inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
			if (ret_mem.length() == sizeof(v)) {
				memcpy(ret_mem.m_begin, &v, sizeof(v));
			}
		}
	}
};

template <typename R, typename C, typename... Args>
struct VariantCaller<R (C::*)(Args...) const> {
	template <int... I>
	static void call(C* inst, auto f, Span<u8> ret_mem, Span<const Variant> args, Indices<I...>& indices) {
		if constexpr (IsSame<R, void>::Value) {
			(inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
		}
		else {
			auto v = (inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
			if (ret_mem.length() == sizeof(v)) {
				memcpy(ret_mem.m_begin, &v, sizeof(v));
			}
		}
	}
};

// DelegateStub can be used to wrap a method as a simple function compatible with Delegate's stub
template <typename T, T f> struct DelegateStub;

template <typename R, typename C, typename... Args, R(C::* F)(Args...)>
struct DelegateStub<R(C::*)(Args...), F> {
	static R stub(void* obj, Args... args) {
		C* o = (C*)obj;
		return (o->*F)(args...);
	}
};

template <typename R, typename C, typename... Args, R(C::* F)(Args...) const>
struct DelegateStub<R(C::*)(Args...) const, F> {
	static R stub(void* obj, Args... args) {
		const C* o = (const C*)obj;
		return (o->*F)(args...);
	}
};

template <auto function> 
struct Function : FunctionBase {
	using F = decltype(function);
	using R = ResultOf<F>::Type;
	using C = ClassOf<F>::Type;

	u32 getArgCount() const override { return ArgsCount<F>::value; }
	TypeDescriptor getReturnType() const override { return toTypeDescriptor<R>(); }
	
	TypeDescriptor getArgType(int i) const override {
		return ArgToTypeDescriptor<F>::get(i);
	}
	
	void invoke(void* obj, Span<u8> ret_mem, Span<const Variant> args) const override {
		auto indices = typename BuildIndices<-1, ArgsCount<F>::value>::result{};
		VariantCaller<F>::call((C*)obj, function, ret_mem, args, indices);
	}

	DummyFnType getDelegateStub() override {
		return reinterpret_cast<DummyFnType>(&DelegateStub<F, function>::stub);
	}
};

template <typename C, typename... Args>
struct Event<DelegateList<void(Args...)>& (C::*)()> : EventBase
{
	using F = DelegateList<void(Args...)>& (C::*)();
	F function;

	u32 getArgCount() const override { return sizeof...(Args); }

	TypeDescriptor getArgType(int i) const override
	{
		TypeDescriptor expand[] = {
			toTypeDescriptor<Args>()...,
			Variant::Type::VOID
		};
		return expand[i];
	}

	template <typename T>
	static Variant toVariant(T value) {
		Variant v;
		v = value;
		return v;
	}

	bool bind(void* object, void* fn_object, FunctionBase* fn) const override {
		C* s = (C*)object;
		using RawFnType = void (*)(void*, Args...);
		if (fn->getArgCount() != ArgsCount<void (C::*)(Args...)>::value) return false;
		for (u32 i = 0; i < fn->getArgCount(); ++i) {
			if (fn->getArgType(i).type != ArgToTypeDescriptor<void (C::*)(Args...)>::get(i).type) {
				return false;
			}
		}

		(s->*function)().bindRaw(fn_object, reinterpret_cast<RawFnType>(fn->getDelegateStub()));
		return true;
	}

	void bind(void* object, Callback* callback) const override {
		C* s = (C*)object;
		auto l = [](void* obj, Args... args) {
			Callback* cb = (Callback*)obj;
			Variant a[] = {
				toVariant(args)...
			};
			cb->invoke(Span(a));
			};
		(s->*function)().bindRaw(callback, l);
	}
};

struct StructVarBase {
	virtual ~StructVarBase() {}
	virtual bool set(void* obj, Span<const u8> mem) = 0;
	virtual bool get(const void* obj, Span<u8> mem) = 0;

	template <typename T> T get(void* obj) {
		T res;
		get(obj, Span((u8*)&res, sizeof(res)));
		return res;
	}

	template <typename T> void set(void* obj, T val) {
		set(obj, Span((const u8*)&val, sizeof(val)));
	}

	virtual TypeDescriptor getType() const = 0;

	const char* name;
};

template <auto Getter>
struct StructVar : StructVarBase {
	using T = typename ResultOf<decltype(Getter)>::Type;
	using C = typename ClassOf<decltype(Getter)>::Type;

	TypeDescriptor getType() const override {
		return toTypeDescriptor<T>();
	}

	bool set(void* obj, Span<const u8> mem) override {
		C* inst = (C*)obj;
		auto& v = inst->*Getter;
		if (sizeof(v) != mem.length()) return false;
		memcpy(&v, mem.begin(), sizeof(v));
		return true;
	}

	bool get(const void* obj, Span<u8> mem) override {
		C* inst = (C*)obj;
		auto& v = inst->*Getter;
		if (sizeof(v) != mem.length()) return false;
		memcpy(mem.begin(), &v, sizeof(v));
		return true;
	}
};

struct LUMIX_ENGINE_API ComponentBase {
	ComponentBase(IAllocator& allocator);

	void visit(IPropertyVisitor& visitor) const;

	const char* icon = "";
	const char* name;
	const char* label;

	CreateComponent creator;
	DestroyComponent destroyer;
	ComponentType component_type;
	Array<PropertyBase*> props;
	Array<FunctionBase*> functions;
};

template <typename T>
bool getPropertyValue(IModule& module, EntityRef e, ComponentType cmp_type, const char* prop_name, T& out) {
	struct : IEmptyPropertyVisitor {
		void visit(const Property<T>& prop) override {
			if (equalStrings(prop.name, prop_name)) {
				found = true;
				value = prop.get(cmp, -1);
			}
		}
		ComponentUID cmp;
		const char* prop_name;
		T value = {};
		bool found = false;
	} visitor;
	visitor.prop_name = prop_name;
	visitor.cmp.module = &module;
	visitor.cmp.type = cmp_type;
	visitor.cmp.entity = e;
	const ComponentBase* cmp_desc = getComponent(cmp_type);
	cmp_desc->visit(visitor);
	out = visitor.value;
	return visitor.found;
}

struct Module {
	Module(IAllocator& allocator);

	Array<FunctionBase*> functions;
	Array<EventBase*> events;
	Array<ComponentBase*> cmps;
	const char* name;
	Module* next = nullptr;
};

LUMIX_ENGINE_API Module* getFirstModule();

struct LUMIX_ENGINE_API builder {
	builder(IAllocator& allocator);

	template <auto Creator, auto Destroyer>
	builder& cmp(const char* name, const char* label) {
		auto creator = [](IModule* module, EntityRef e){ (module->*static_cast<void (IModule::*)(EntityRef)>(Creator))(e); };
		auto destroyer = [](IModule* module, EntityRef e){ (module->*static_cast<void (IModule::*)(EntityRef)>(Destroyer))(e); };
	
		ComponentBase* cmp = LUMIX_NEW(allocator, ComponentBase)(allocator);
		cmp->name = name;
		cmp->label = label;
		cmp->component_type = getComponentType(name);
		cmp->creator = creator;
		cmp->destroyer = destroyer;
		registerCmp(cmp);

		return *this;
	}

	template <bool pick_first, typename A, typename B> struct Pick { using Type = A; };
	template <typename A, typename B> struct Pick<false, A, B> { using Type = B; };

	template <auto Getter, auto Setter = nullptr>
	builder& prop(const char* name) {
		using T = typename ResultOf<decltype(Getter)>::Type;
		using Backing = typename Pick<__is_enum(T), i32, T>::Type;
		auto* p = LUMIX_NEW(allocator, Property<Backing>)(allocator);
		
		if constexpr (Setter == nullptr) {
			p->setter = nullptr;
		}
		else if constexpr (__is_enum(T)) {
			p->setter = [](IModule* module, EntityRef e, u32 idx, const i32& value) {
				using C = typename ClassOf<decltype(Setter)>::Type;
				if constexpr (ArgsCount<decltype(Setter)>::value == 2) {
					(static_cast<C*>(module)->*Setter)(e, static_cast<T>(value));
				}
				else {
					(static_cast<C*>(module)->*Setter)(e, idx, static_cast<T>(value));
				}
			};
		}
		else {
			p->setter = [](IModule* module, EntityRef e, u32 idx, const T& value) {
				using C = typename ClassOf<decltype(Setter)>::Type;
				if constexpr (ArgsCount<decltype(Setter)>::value == 2) {
					(static_cast<C*>(module)->*Setter)(e, value);
				}
				else {
					(static_cast<C*>(module)->*Setter)(e, idx, value);
				}
			};
		}

		if constexpr (__is_enum(T)) {
			p->getter = [](IModule* module, EntityRef e, u32 idx) -> i32 {
				using C = typename ClassOf<decltype(Getter)>::Type;
				if constexpr (ArgsCount<decltype(Getter)>::value == 1) {
					return static_cast<i32>((static_cast<C*>(module)->*Getter)(e));
				}
				else {
					return static_cast<i32>((static_cast<C*>(module)->*Getter)(e, idx));
				}
			};
		}
		else {
			p->getter = [](IModule* module, EntityRef e, u32 idx) -> T {
				using C = typename ClassOf<decltype(Getter)>::Type;
				if constexpr (ArgsCount<decltype(Getter)>::value == 1) {
					return (static_cast<C*>(module)->*Getter)(e);
				}
				else {
					return (static_cast<C*>(module)->*Getter)(e, idx);
				}
			};
		}

		p->name = name;
		addProp(p);
		return *this;
	}

	template <auto Getter, auto Setter>
	builder& blob_property(const char* name) {
		auto* p = LUMIX_NEW(allocator, BlobProperty)(allocator);
		p->name = name;
		p->setter = [](IModule* module, EntityRef e, u32 idx, InputMemoryStream& value) {
			using C = typename ClassOf<decltype(Setter)>::Type;
			if constexpr (ArgsCount<decltype(Setter)>::value == 2) {
				(static_cast<C*>(module)->*Setter)(e, value);
			}
			else {
				(static_cast<C*>(module)->*Setter)(e, idx, value);
			}
		};
		p->getter = [](IModule* module, EntityRef e, u32 idx, OutputMemoryStream& value) {
			using C = typename ClassOf<decltype(Getter)>::Type;
			if constexpr (ArgsCount<decltype(Getter)>::value == 2) {
				(static_cast<C*>(module)->*Getter)(e, value);
			}
			else {
				(static_cast<C*>(module)->*Getter)(e, idx, value);
			}
		};
		addProp(p);
		return *this;
	}

	template <auto Getter, auto PropGetter>
	builder& var_prop(const char* name) {
		using T = typename ResultOf<decltype(PropGetter)>::Type;
		auto* p = LUMIX_NEW(allocator, Property<T>)(allocator);
		p->setter = [](IModule* module, EntityRef e, u32, const T& value) {
			using C = typename ClassOf<decltype(Getter)>::Type;
			auto& c = (static_cast<C*>(module)->*Getter)(e);
			auto& v = c.*PropGetter;
			v = value;
		};
		p->getter = [](IModule* module, EntityRef e, u32) -> T {
			using C = typename ClassOf<decltype(Getter)>::Type;
			auto& c = (static_cast<C*>(module)->*Getter)(e);
			auto& v = c.*PropGetter;
			return static_cast<T>(v);
		};
		p->name = name;
		addProp(p);
		return *this;
	}

	template <auto Counter, auto Adder, auto Remover>
	builder& begin_array(const char* name) {
		ArrayProperty* prop = LUMIX_NEW(allocator, ArrayProperty)(allocator);
		using C = typename ClassOf<decltype(Counter)>::Type;
		prop->counter = [](IModule* module, EntityRef e) -> u32 {
			return (static_cast<C*>(module)->*Counter)(e);
		};
		prop->adder = [](IModule* module, EntityRef e, u32 idx) {
			(static_cast<C*>(module)->*Adder)(e, idx);
		};
		prop->remover = [](IModule* module, EntityRef e, u32 idx) {
			(static_cast<C*>(module)->*Remover)(e, idx);
		};
		prop->name = name;
		module->cmps.back()->props.push(prop);
		array = prop;
		last_prop = prop;
		return *this;
	}

	template <typename T>
	builder& attribute() {
		auto* a = LUMIX_NEW(allocator, T);
		last_prop->attributes.push(a);
		return *this;
	}

	template <auto F>
	builder& event(const char* name) {
		auto* f = LUMIX_NEW(allocator, Event<decltype(F)>);
		f->function = F;
		f->name = name;
		module->events.push(f);
		return *this;
	}

	template <auto F>
	builder& function(const char* name) {
		auto* f = LUMIX_NEW(allocator, Function<F>);
		f->name = name;
		if (module->cmps.empty()) {
			module->functions.push(f);
		}
		else {
			module->cmps.back()->functions.push(f);
		}
		return *this;
	}

	void registerCmp(ComponentBase* cmp);

	builder& minAttribute(float value);
	builder& clampAttribute(float min, float max);
	builder& resourceAttribute(ResourceType type);
	builder& radiansAttribute();
	builder& colorAttribute();
	builder& noUIAttribute();
	builder& multilineAttribute();
	builder& icon(const char* icon);
	builder& end_array();

	void addProp(PropertyBase* prop);

	IAllocator& allocator;
	Module* module;
	ArrayProperty* array = nullptr;
	PropertyBase* last_prop = nullptr;
};

template <typename F> void forEachProperty(ComponentType cmp_type, const F& f) {
	struct Helper final : public IPropertyVisitor {
		Helper(const F& f) : f(f) {}
		void visit(const Property<float>& prop) override { f(prop, parent);}
		void visit(const Property<int>& prop) override { f(prop, parent); }
		void visit(const Property<u32>& prop) override { f(prop, parent); }
		void visit(const Property<EntityPtr>& prop) override { f(prop, parent); }
		void visit(const Property<Vec2>& prop) override { f(prop, parent); }
		void visit(const Property<Vec3>& prop) override { f(prop, parent); }
		void visit(const Property<IVec3>& prop) override { f(prop, parent); }
		void visit(const Property<Vec4>& prop) override { f(prop, parent); }
		void visit(const Property<Path>& prop) override { f(prop, parent); }
		void visit(const Property<bool>& prop) override { f(prop, parent); }
		void visit(const Property<const char*>& prop) override { f(prop, parent); }
		void visit(const struct ArrayProperty& prop) override {
			f(prop, parent); 
			parent = &prop;
			prop.visitChildren(*this);
			parent = nullptr;
		}
		void visit(const struct BlobProperty& prop) override { f(prop, parent); }
		const F& f;
		const ArrayProperty* parent = nullptr;
	};

	Helper h(f);
	const ComponentBase* cmp = getComponent(cmp_type);
	if (cmp) cmp->visit(h);
}

LUMIX_ENGINE_API builder build_module(const char* name);

} // namespace reflection

} // namespace Lumix
