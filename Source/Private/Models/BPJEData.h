#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"

enum class EBPJEPinType : uint8
{
	Exec,
	Boolean,
	Byte,
	Integer,
	Integer64,
	Float,
	Double,
	Real,
	String,
	Name,
	Text,
	Vector,
	Vector2D,
	Vector4D,
	Rotator,
	Transform,
	Quat,
	Object,
	Class,
	Interface,
	Struct,
	Enum,
	Delegate,
	MulticastDelegate,
	SoftObject,
	SoftClass,
	AssetId,
	Material,
	Texture,
	StaticMesh,
	SkeletalMesh,
	Pose,
	Animation,
	BlendSpace,
	FieldPath,
	Bitmask,
	Self,
	Index,
	Wildcard
};

enum class EBPJEPinContainerType : uint8
{
	None,
	Array,
	Set,
	Map
};

enum class EBPJENodeType : uint8
{
	CallFunction,
	CallArrayFunction,
	CallDataTableFunction,
	CallDelegate,
	CallFunctionOnMember,
	CallMaterialParameterCollection,
	CallParentFunction,
	GenericToText,
	GetDataTableRow,
	FunctionEntry,
	FunctionResult,
	FunctionTerminator,
	VariableSet,
	VariableGet,
	VariableSetRef,
	Variable,
	LocalVariable,
	LocalVariableSet,
	LocalVariableGet,
	FunctionParameter,
	LocalFunctionVariable,
	MakeVariable,
	TemporaryVariable,
	SetVariableOnPersistentFrame,
	Event,
	CustomEvent,
	ActorBoundEvent,
	ComponentBoundEvent,
	GeneratedBoundEvent,
	GetInputAxisKeyValue,
	GetInputAxisValue,
	GetInputVectorAxisValue,
	EventNodeInterface,
	InputAction,
	InputActionEvent,
	InputAxisEvent,
	InputAxisKeyEvent,
	InputKey,
	InputKeyEvent,
	InputTouch,
	InputTouchEvent,
	InputVectorAxisEvent,
	ForEachLoop,
	ForEachElementInEnum,
	WhileLoop,
	ForLoop,
	Sequence,
	Branch,
	Select,
	Gate,
	MultiGate,
	DoOnceMultiInput,
	DoOnce,
	Knot,
	Tunnel,
	TunnelBoundary,
	Switch,
	SwitchInt,
	SwitchString,
	SwitchEnum,
	SwitchName,
	MakeStruct,
	BreakStruct,
	SetFieldsInStruct,
	StructMemberGet,
	StructMemberSet,
	StructOperation,
	MakeArray,
	MakeMap,
	MakeSet,
	MakeContainer,
	GetArrayItem,
	DynamicCast,
	ClassDynamicCast,
	CastByteToEnum,
	ConvertAsset,
	EditablePinBase,
	ExternalGraphInterface,
	AddDelegate,
	CreateDelegate,
	ClearDelegate,
	RemoveDelegate,
	AssignDelegate,
	DelegateSet,
	AsyncAction,
	BaseAsyncTask,
	AddComponent,
	AddComponentByClass,
	AddPinInterface,
	ConstructObjectFromClass,
	GenericCreateObject,
	Timeline,
	SpawnActor,
	SpawnActorFromClass,
	FormatText,
	GetClassDefaults,
	GetSubsystem,
	LoadAsset,
	Copy,
	Comment,
	BitmaskLiteral,
	EnumEquality,
	EnumInequality,
	EnumLiteral,
	GetEnumeratorName,
	GetEnumeratorNameAsString,
	GetNumEnumEntries,
	MathExpression,
	EaseFunction,
	CommutativeAssociativeBinaryOperator,
	PureAssignmentStatement,
	AssignmentStatement,
	Self,
	Composite,
	DeadClass,
	Literal,
	Message,
	PromotableOperator,
	MacroInstance,
	BaseMCDelegate,
};

enum class EBPJEBlueprintType : uint8
{
	Normal,
	Const,
	MacroLibrary,
	Interface,
	LevelScript,
	FunctionLibrary,
};

enum class EBPJEStructMemberType : uint8
{
	Bool,
	Byte,
	Int,
	Float,
	String,
	Name,
	Text,
	Vector,
	Vector2D,
	Rotator,
	Transform,
	Class,
	Object,
	Struct,
	Enum,
	Array,
	Set,
	Map,
	Custom
};

struct FBPJEStructMember
{
	EBPJEStructMemberType Type = EBPJEStructMemberType::Int;
	EBPJEStructMemberType KeyType = EBPJEStructMemberType::Int;

	bool bIsArray = false;
	bool bIsSet = false;
	bool bIsMap = false;

	FString Name;
	FString TypeName;
	FString KeyTypeName;
	FString DefaultValue;
	FString Comment;
};

struct FBPJEStruct
{
	FString Name;
	FString Comment;
	TArray<FBPJEStructMember> Members;
};

struct FBPJEEnumValue
{
	FString Name;
	FString Comment;
};

struct FBPJEEnum
{
	FString Name;
	FString Comment;
	TArray<FBPJEEnumValue> Values;
};

enum class EBPJEGraphType : uint8
{
	EventGraph,
	Function,
	Composite,
	Macro,
	Construction,
	Animation,
	Struct,
	Enum
};
