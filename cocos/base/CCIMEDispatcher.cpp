/****************************************************************************
Copyright (c) 2010      cocos2d-x.org
Copyright (C) 2013-2014 Chukong Technologies Inc.

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "base/CCIMEDispatcher.h"
#include <algorithm>

NS_CC_BEGIN

//////////////////////////////////////////////////////////////////////////
// add/remove delegate in IMEDelegate Cons/Destructor
//////////////////////////////////////////////////////////////////////////

IMEDelegate::IMEDelegate(){
    IMEDispatcher::sharedDispatcher()->addDelegate(this);
}

IMEDelegate::~IMEDelegate(){
    IMEDispatcher::sharedDispatcher()->removeDelegate(this);
}

bool IMEDelegate::attachWithIME(){
    return IMEDispatcher::sharedDispatcher()->attachDelegateWithIME(this);
}

bool IMEDelegate::detachWithIME(){
    return IMEDispatcher::sharedDispatcher()->detachDelegateWithIME(this);
}

bool IMEDelegate::isAttached(){
	return IMEDispatcher::sharedDispatcher()->isAttached(this);
}

//////////////////////////////////////////////////////////////////////////
// Cons/Destructor
//////////////////////////////////////////////////////////////////////////

IMEDispatcher::IMEDispatcher(){
    target = nullptr;
}

IMEDispatcher::~IMEDispatcher(){

}

//////////////////////////////////////////////////////////////////////////
// Add/Attach/Remove IMEDelegate
//////////////////////////////////////////////////////////////////////////

void IMEDispatcher::addDelegate(IMEDelegate* delegate){
    if(!delegate){
        return;
    }
    auto old = find(delegates.begin(), delegates.end(), delegate);
    if(old != delegates.end()){
        return;
    }
    delegates.push_back(delegate);
}

bool IMEDispatcher::attachDelegateWithIME(IMEDelegate * delegate){
    if(target && target != delegate && target->canDetachWithIME()){
        target->didDetachWithIME();
        target = nullptr;
    }
    if(delegate && delegate->canAttachWithIME()){
        delegate->didAttachWithIME();
    }
    target = delegate;
    return true;
}

bool IMEDispatcher::detachDelegateWithIME(IMEDelegate * delegate){
    if(!target || delegate != target){
        return false;
    }
    if(delegate->canDetachWithIME()){
        delegate->didDetachWithIME();
    }
    target = nullptr;
    return true;
}

bool IMEDispatcher::isAttached(IMEDelegate* delegate){
	if(target && target == delegate){
		return true;
	}
	return false;
}

void IMEDispatcher::removeDelegate(IMEDelegate* delegate){
    auto old = find(delegates.begin(), delegates.end(), delegate);
    if(old != delegates.end()){
        swap(delegates.back(), *old);
        delegates.pop_back();
    }
    if(delegate == target){
        target = nullptr;
    }
}

void IMEDispatcher::removeAll(){
	delegates.clear();
    target = nullptr;
}

//////////////////////////////////////////////////////////////////////////
// dispatch text message
//////////////////////////////////////////////////////////////////////////

void IMEDispatcher::dispatchInsertText(const char * text, size_t len){
    if(target && text && len > 0){
        target->insertText(text, len);
    }
}

void IMEDispatcher::dispatchDeleteBackward(){
    if(target){
        target->deleteBackward();
    }
}

void IMEDispatcher::dispatchControl(int key){
    if(target){
        target->controlKey(key);
    }
}

const std::string& IMEDispatcher::getContentText(){
    if(target){
        return target->getContentText();
    }
    return STD_STRING_EMPTY;
}

//////////////////////////////////////////////////////////////////////////
// dispatch keyboard message
//////////////////////////////////////////////////////////////////////////

void IMEDispatcher::dispatchKeyboardWillShow(IMEKeyboardNotificationInfo& info){
    for(int i=0;i<delegates.size();i++){
        delegates[i]->keyboardWillShow(info);
    }
}

void IMEDispatcher::dispatchKeyboardDidShow(IMEKeyboardNotificationInfo& info){
    for(int i=0;i<delegates.size();i++){
        delegates[i]->keyboardDidShow(info);
    }
}

void IMEDispatcher::dispatchKeyboardWillHide(IMEKeyboardNotificationInfo& info){
    for(int i=0;i<delegates.size();i++){
        delegates[i]->keyboardWillHide(info);
    }
}

void IMEDispatcher::dispatchKeyboardDidHide(IMEKeyboardNotificationInfo& info){
    for(int i=0;i<delegates.size();i++){
        delegates[i]->keyboardDidHide(info);
    }
}

//////////////////////////////////////////////////////////////////////////
// static member function
//////////////////////////////////////////////////////////////////////////

IMEDispatcher* IMEDispatcher::sharedDispatcher()
{
    static IMEDispatcher s_instance;
    return &s_instance;
}

NS_CC_END
