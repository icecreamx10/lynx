// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0.
#include "platform/wasm/devtool/native.h"
#include <sstream>
#include <charconv>
#include <iomanip>
#include "core/renderer/dom/fiber/raw_text_element.h"
#include "core/renderer/starlight/layout/layout_object.h"
#include "core/renderer/starlight/style/layout_computed_style.h"
namespace lynx::wasm {
namespace {
Json::Value Object() { return Json::Value(Json::objectValue); }
Json::Value Array() { return Json::Value(Json::arrayValue); }
std::string Encode(const Json::Value& value) { Json::StreamWriterBuilder b; b["indentation"]=""; return Json::writeString(b,value); }
int ID(tasm::Element* n) { return n->impl_id()+2; }
int SheetID(const std::string& s) { int id=0; if(s.size()<8)return 0;auto r=std::from_chars(s.data()+7,s.data()+s.size(),id);return r.ec==std::errc()&&r.ptr==s.data()+s.size()?id:0; }
std::string Color(tasm::Element* n, tasm::CSSPropertyID id) {
 auto c=n->computed_css_style()->ExtractCanonicalComputedValue(id);
 if(!c || c->kind()!=starlight::CanonicalComputedValue::Kind::kColor) return "transparent";
 uint32_t v=std::get<uint32_t>(c->storage());
 return "rgba("+std::to_string((v>>16)&255)+","+std::to_string((v>>8)&255)+","+std::to_string(v&255)+","+std::to_string((v>>24)/255.f)+")";
}
}
void BrowserDevTool::Event(const std::string& method,const Json::Value& params) {
 auto event=Object();event["method"]=method;event["params"]=params;emit_(Encode(event));
}
void BrowserDevTool::Reset() { snapshot_.clear();highlight_=0; if(dom_enabled_) Event("DOM.documentUpdated",Object()); }
tasm::Element* BrowserDevTool::Find(int id) {
 std::function<tasm::Element*(tasm::Element*)> walk=[&](tasm::Element* n)->tasm::Element*{
  if(!n)return nullptr;if(ID(n)==id)return n;
  for(auto* c=n->first_child();c;c=c->next_sibling())if(auto* f=walk(c))return f;
  return nullptr;
 };return walk(root_());
}
Json::Value BrowserDevTool::Node(tasm::Element* n,int depth) {
 auto j=Object();j["nodeId"]=ID(n);j["backendNodeId"]=ID(n);
 j["nodeType"]=n->is_raw_text()?3:1;j["nodeName"]=n->is_raw_text()?"#text":n->GetTag().str();
 j["localName"]=n->is_raw_text()?"":n->GetTag().str();j["nodeValue"]=n->is_raw_text()?static_cast<tasm::RawTextElement*>(n)->content().str():"";
 j["attributes"]=Array();
 if(!n->GetIdSelector().empty()){j["attributes"].append("id");j["attributes"].append(n->GetIdSelector().str());}
 if(!n->classes().empty()){std::string classes;for(const auto& c:n->classes()){if(!classes.empty())classes+=' ';classes+=c.str();}j["attributes"].append("class");j["attributes"].append(classes);}
 auto css=n->GetRawInlineStyles().str();if(!css.empty()){j["attributes"].append("style");j["attributes"].append(css);}
 int count=0;auto children=Array();for(auto* c=n->first_child();c;c=c->next_sibling()){++count;if(depth!=0)children.append(Node(c,depth<0?-1:depth-1));}
 j["childNodeCount"]=count;if(depth!=0)j["children"]=children;return j;
}
Json::Value BrowserDevTool::Box(tasm::Element* n) {
 auto j=Object();if(!n || !n->slnode())return j;
 const auto& b=n->slnode()->GetLayoutResult();float x=b.offset_.X(),y=b.offset_.Y();
 for(auto* p=n->render_parent();p;p=p->render_parent())if(p->slnode()){x+=p->slnode()->GetLayoutResult().offset_.X();y+=p->slnode()->GetLayoutResult().offset_.Y();}
 float w=b.size_.width_,h=b.size_.height_;auto quad=Array();for(float v:{x,y,x+w,y,x+w,y+h,x,y+h})quad.append(v);
 for(auto key:{"content","padding","border","margin"})j[key]=quad;j["width"]=w;j["height"]=h;return j;
}
Json::Value BrowserDevTool::Style(tasm::Element* n) {
 auto j=Object();j["styleSheetId"]="inline."+std::to_string(ID(n));auto text=n->GetRawInlineStyles().str();j["cssText"]=text;
 j["cssProperties"]=Array();j["shorthandEntries"]=Array();
 auto range=Object();range["startLine"]=0;range["startColumn"]=0;range["endLine"]=0;range["endColumn"]=static_cast<int>(text.size());j["range"]=range;
 size_t at=0;while(at<text.size()){auto end=text.find(';',at);if(end==std::string::npos)end=text.size();auto colon=text.find(':',at);if(colon<end){auto p=Object();p["name"]=text.substr(at,colon-at);p["value"]=text.substr(colon+1,end-colon-1);p["text"]=text.substr(at,end-at)+(end<text.size()?";":"");range["startColumn"]=static_cast<int>(at);range["endColumn"]=static_cast<int>(end+(end<text.size()));p["range"]=range;p["parsedOk"]=true;j["cssProperties"].append(p);}at=end+1;}return j;
}
void BrowserDevTool::Poll() {
 if(!dom_enabled_||!document_sent_||!root_())return;
 std::unordered_map<int,Json::Value> current;
 std::function<void(tasm::Element*)> walk=[&](tasm::Element* n){auto j=Node(n,0);auto children=Array();for(auto*c=n->first_child();c;c=c->next_sibling()){children.append(ID(c));walk(c);}j["childIds"]=children;current.emplace(ID(n),std::move(j));};walk(root_());
 bool structural=!snapshot_.empty()&&snapshot_.size()!=current.size();
 for(const auto& [id,n]:current){auto old=snapshot_.find(id);if(old==snapshot_.end()){if(!snapshot_.empty())structural=true;continue;}if(old->second["childIds"]!=n["childIds"])structural=true;
  if(old->second["nodeValue"]!=n["nodeValue"]){auto p=Object();p["nodeId"]=id;p["characterData"]=n["nodeValue"];Event("DOM.characterDataModified",p);}
 }
 snapshot_=std::move(current);if(structural)Event("DOM.documentUpdated",Object());
}
void BrowserDevTool::Dispatch(const std::string& raw) {
 Json::Value req;Json::CharReaderBuilder reader;std::string errors;std::istringstream input(raw);
 if(!Json::parseFromStream(reader,input,&req,&errors)||!req.isObject()||!req["method"].isString()||!req["id"].isIntegral())return;
 auto res=Object();res["id"]=req["id"];if(req.isMember("sessionId"))res["sessionId"]=req["sessionId"];
 auto result=Object();const auto m=req["method"].asString();const auto& p=req["params"];
 auto error=[&](int code,const std::string& message){res["error"]["code"]=code;res["error"]["message"]=message;emit_(Encode(res));};
 if(m.rfind("Runtime.",0)==0||m.rfind("Debugger.",0)==0){if(runtime_){runtime_(raw);return;}error(-32601,"JS inspector is not attached");return;}
 auto* n=Find(p.get("nodeId",p.get("backendNodeId",0)).asInt());
 if(m=="DOM.enable"){dom_enabled_=true;document_sent_=false;snapshot_.clear();}
 else if(m=="DOM.disable")dom_enabled_=false;
 else if(m=="DOM.getDocument"){
  document_sent_=true;snapshot_.clear();
  auto doc=Object();doc["nodeId"]=1;doc["backendNodeId"]=1;doc["nodeType"]=9;doc["nodeName"]="#document";doc["localName"]="";doc["nodeValue"]="";doc["documentURL"]="lynx://canvas/main.lynx.bundle";doc["baseURL"]=doc["documentURL"];doc["childNodeCount"]=root_()?1:0;doc["children"]=Array();if(root_())doc["children"].append(Node(root_()));result["root"]=doc;
 }else if(m=="DOM.requestChildNodes"){
  auto e=Object();e["parentId"]=p["nodeId"];e["nodes"]=Array();if(n)for(auto*c=n->first_child();c;c=c->next_sibling())e["nodes"].append(Node(c));Event("DOM.setChildNodes",e);
 }else if(m=="DOM.describeNode"&&n)result["node"]=Node(n);
 else if(m=="DOM.getAttributes"&&n)result["attributes"]=Node(n,0)["attributes"];
 else if(m=="DOM.getBoxModel"&&n)result["model"]=Box(n);
 else if(m=="CSS.getComputedStyleForNode"&&n){
  result["computedStyle"]=Array();auto add=[&](const char* name,std::string value){auto v=Object();v["name"]=name;v["value"]=value;result["computedStyle"].append(v);};auto box=Box(n);
  add("width",std::to_string(box.get("width",0).asFloat())+"px");add("height",std::to_string(box.get("height",0).asFloat())+"px");add("background-color",Color(n,tasm::kPropertyIDBackgroundColor));add("color",Color(n,tasm::kPropertyIDColor));add("font-size",std::to_string(n->GetFontSize())+"px");
 }else if((m=="CSS.getInlineStylesForNode"||m=="CSS.getMatchedStylesForNode")&&n){result["inlineStyle"]=Style(n);if(m=="CSS.getMatchedStylesForNode"){result["matchedCSSRules"]=Array();result["inherited"]=Array();}}
 else if(m=="CSS.getStyleSheetText"){
  auto id=p["styleSheetId"].asString();if(id.rfind("inline.",0)!=0){error(-32602,"Unknown stylesheet");return;}n=Find(SheetID(id));if(!n){error(-32602,"Stale stylesheet");return;}result["text"]=n->GetRawInlineStyles().str();
 }else if(m=="CSS.setStyleTexts"){
  result["styles"]=Array();for(auto& edit:p["edits"]){const auto id=edit["styleSheetId"].asString();if(id.rfind("inline.",0)!=0){error(-32602,"Only inline styles are editable");return;}n=Find(SheetID(id));if(!n){error(-32602,"Stale stylesheet");return;}auto text=n->GetRawInlineStyles().str();const auto&r=edit["range"];int start=r["startColumn"].asInt(),end=r["endColumn"].asInt();if(r["startLine"].asInt()!=0||r["endLine"].asInt()!=0||start<0||end<start||end>static_cast<int>(text.size())){error(-32602,"Invalid inline range");return;}text.replace(start,end-start,edit["text"].asString());n->SetRawInlineStyles(base::String(text));flush_();result["styles"].append(Style(n));auto e=Object();e["styleSheetId"]=id;Event("CSS.styleSheetChanged",e);}
 }else if(m=="Overlay.highlightNode"){if(!n){error(-32602,"Unknown node");return;}highlight_=ID(n);flush_();}
 else if(m=="Overlay.hideHighlight"){highlight_=0;flush_();}
 else if(m=="Page.getResourceTree"){auto& frame=result["frameTree"]["frame"];frame["id"]="canvas";frame["loaderId"]="bundle";frame["url"]="lynx://canvas/main.lynx.bundle";frame["securityOrigin"]="lynx://canvas";frame["mimeType"]="application/lynx";result["frameTree"]["resources"]=Array();}
 else if(m=="Page.getFrameTree"){result["frameTree"]["frame"]["id"]="canvas";result["frameTree"]["frame"]["url"]="lynx://canvas/main.lynx.bundle";}
 else if(m=="CSS.enable") {
  std::function<void(tasm::Element*)> sheets=[&](tasm::Element* node){if(!node)return;auto h=Object();h["styleSheetId"]="inline."+std::to_string(ID(node));h["frameId"]="canvas";h["sourceURL"]="lynx://canvas/inline/"+std::to_string(ID(node));h["origin"]="regular";h["title"]="";h["disabled"]=false;h["isInline"]=true;h["startLine"]=0;h["startColumn"]=0;h["length"]=static_cast<int>(node->GetRawInlineStyles().str().size());auto e=Object();e["header"]=h;Event("CSS.styleSheetAdded",e);for(auto*c=node->first_child();c;c=c->next_sibling())sheets(c);};sheets(root_());
 }
 else if(m=="CSS.disable"||m=="Overlay.enable"||m=="Overlay.disable"||m=="Page.enable"||m=="Page.disable"||m=="Log.enable"||m=="Log.disable"||m=="Inspector.enable"||m=="Inspector.disable"||m=="DOM.markUndoableState"){}
 else {error(n?-32601:-32602,"Unsupported browser-host method: "+m);return;}
 res["result"]=result;emit_(Encode(res));
}
}  // namespace lynx::wasm
