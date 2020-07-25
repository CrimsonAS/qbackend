package qbackend

import "sync"

type InvokedPromise struct {
	mutex    sync.Mutex
	callback func(values []interface{}, err error)
	err      error
	values   []interface{}
	result   bool
	used     bool
}

func NewInvokedPromise() *InvokedPromise {
	return &InvokedPromise{}
}

func (p *InvokedPromise) setCallback(callback func(values []interface{}, err error)) {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	if p.used {
		panic("InvokedPromise has already been used")
	}
	p.callback = callback
	if p.result {
		p.used = true
		p.callback(p.values, p.err)
		return
	}
}

func (p *InvokedPromise) Reject(err error) {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	if p.used || p.result {
		panic("InvokedPromise has already been used")
	}
	p.err = err
	p.result = true
	if p.callback != nil {
		p.used = true
		p.callback(nil, p.err)
	}
}

func (p *InvokedPromise) Resolve(values ...interface{}) {
	p.mutex.Lock()
	defer p.mutex.Unlock()
	if p.used || p.result {
		panic("InvokedPromise has already been used")
	}
	p.values = values
	p.result = true
	if p.callback != nil {
		p.used = true
		p.callback(p.values, nil)
	}
}
