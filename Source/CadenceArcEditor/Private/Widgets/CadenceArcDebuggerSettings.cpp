#include "CadenceArcDebuggerSettings.h"

#include "Layout/CadenceArcGraphLayout.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* DebuggerConfigSection = TEXT("CadenceArc.Debugger");
}

FCadenceArcDebuggerSettings FCadenceArcDebuggerSettings::Load()
{
	FCadenceArcDebuggerSettings Settings;
	GConfig->GetBool(DebuggerConfigSection, TEXT("bOrthogonalEdges"), Settings.bOrthogonalEdges, GEditorPerProjectIni);
	GConfig->GetBool(DebuggerConfigSection, TEXT("bSortPorts"), Settings.bSortPorts, GEditorPerProjectIni);
	GConfig->GetBool(DebuggerConfigSection, TEXT("bCompactChains"), Settings.bCompactChains, GEditorPerProjectIni);
	GConfig->GetBool(DebuggerConfigSection, TEXT("bUseReferences"), Settings.bUseReferences, GEditorPerProjectIni);
	GConfig->GetInt(DebuggerConfigSection, TEXT("ReferenceMinSpan"), Settings.ReferenceMinSpan, GEditorPerProjectIni);
	Settings.ReferenceMinSpan = FMath::Clamp(Settings.ReferenceMinSpan, 2, 9);
	return Settings;
}

void FCadenceArcDebuggerSettings::Save() const
{
	GConfig->SetBool(DebuggerConfigSection, TEXT("bOrthogonalEdges"), bOrthogonalEdges, GEditorPerProjectIni);
	GConfig->SetBool(DebuggerConfigSection, TEXT("bSortPorts"), bSortPorts, GEditorPerProjectIni);
	GConfig->SetBool(DebuggerConfigSection, TEXT("bCompactChains"), bCompactChains, GEditorPerProjectIni);
	GConfig->SetBool(DebuggerConfigSection, TEXT("bUseReferences"), bUseReferences, GEditorPerProjectIni);
	GConfig->SetInt(DebuggerConfigSection, TEXT("ReferenceMinSpan"), ReferenceMinSpan, GEditorPerProjectIni);
}

void FCadenceArcDebuggerSettings::ApplyTo(FCadenceArcLayoutParams& Params) const
{
	Params.EdgeStyle = bOrthogonalEdges ? ECadenceArcEdgeStyle::Orthogonal : ECadenceArcEdgeStyle::Curved;
	Params.bSortPorts = bSortPorts;
	Params.Mode = bCompactChains ? ECadenceArcLayoutMode::CompactChains : ECadenceArcLayoutMode::Layered;
	Params.ReferenceMinSpan = bUseReferences ? ReferenceMinSpan : 0;
}
