#include <vector>
#include <exception>
#include <map>
#include <stdexcept>
#include "sortedmap.h"

const char *sortedmap::keyiter::name = "sortedmap.keyiter";
const char *sortedmap::valiter::name = "sortedmap.valiter";
const char *sortedmap::itemiter::name = "sortedmap.itemiter";
const char *sortedmap::keyview::name = "sortedmap.keyview";
const char *sortedmap::valview::name = "sortedmap.valview";
const char *sortedmap::itemview::name = "sortedmap.itemview";

PyObject*
py_identity(PyObject *ob) {
    Py_INCREF(ob);
    return ob;
}

PyObject *
sortedmap::Comparator::call(PyObject *ob) {
    PyObject *ret;

    PyTuple_SET_ITEM(argtuple, 0, ob);
    ret = PyObject_Call(keyfunc, argtuple, NULL);
    if (Py_REFCNT(argtuple) != 1) {
        Py_INCREF(ob);
        Py_DECREF(argtuple);
        if (unlikely(!(argtuple = PyTuple_New(1)))) {
            throw PythonError();
        }
    }
    else {
        PyTuple_SET_ITEM(argtuple, 0, NULL);
    }
    return ret;
}

sortedmap::Comparator::Comparator() {
    this->keyfunc = NULL;
    argtuple = NULL;
}

sortedmap::Comparator::Comparator(PyObject *keyfunc) {
    this->keyfunc = keyfunc;
    Py_XINCREF(keyfunc);
    argtuple = NULL;
}

sortedmap::Comparator::~Comparator() {
    Py_XDECREF(keyfunc);
    Py_XDECREF(argtuple);
}

sortedmap::Comparator&
sortedmap::Comparator::operator=(const Comparator &other) {
    keyfunc = other.keyfunc;
    Py_XINCREF(keyfunc);
    argtuple = NULL;
    return *this;
}

bool
sortedmap::Comparator::operator()(const OwnedRef<PyObject> &a,
                                  const OwnedRef<PyObject> &b){
    if (!keyfunc) {
        return a < b;
    }

    if (unlikely(!argtuple)) {
        if (!(argtuple = PyTuple_New(1))) {
            throw PythonError();
        }
    }

    OwnedRef<PyObject> a_ob;
    OwnedRef<PyObject> b_ob;
    int status;

    if (unlikely(!(a_ob = call(a)) || !(b_ob = call(b)))) {
        throw PythonError();
    }
    status = PyObject_RichCompareBool(a_ob, b_ob, Py_LT);
    if (unlikely(status < 0)) {
        throw PythonError();
    }
    return status;
}

bool
sortedmap::check(PyObject *ob) {
    return PyObject_IsInstance(ob, (PyObject*) &sortedmap::type);
}

bool
sortedmap::check_exact(PyObject *ob) {
    return Py_TYPE(ob) == &sortedmap::type;
}

void
sortedmap::abstractiter::dealloc(sortedmap::abstractiter::object *self) {
    using sortedmap::abstractiter::itertype;
    using ownedtype = OwnedRef<sortedmap::object>;

    self->iter.~itertype();
    self->map.~ownedtype();
    PyObject_Del(self);
}

PyObject*
sortedmap::keyiter::elem(sortedmap::abstractiter::itertype it) {
    return std::get<0>(*it).incref();
}

PyObject*
sortedmap::valiter::elem(sortedmap::abstractiter::itertype it) {
    return std::get<1>(*it).incref();
}

PyObject*
sortedmap::itemiter::elem(sortedmap::abstractiter::itertype it) {
    return PyTuple_Pack(2,
                        sortedmap::keyiter::elem(it),
                        sortedmap::valiter::elem(it));
}

PyObject*
sortedmap::keyiter::iter(sortedmap::object *self) {
    return sortedmap::abstractiter::iter<sortedmap::keyiter::object,
                                         sortedmap::keyiter::type>(self);
}

PyObject*
sortedmap::valiter::iter(sortedmap::object *self) {
    return sortedmap::abstractiter::iter<sortedmap::valiter::object,
                                         sortedmap::valiter::type>(self);
}

PyObject*
sortedmap::itemiter::iter(sortedmap::object *self) {
    return sortedmap::abstractiter::iter<sortedmap::itemiter::object,
                                         sortedmap::itemiter::type>(self);
}

PyObject*
sortedmap::abstractview::repr(sortedmap::abstractview::object *self) {
    PyObject *aslist;
    PyObject *ret;

    if (!(aslist = PySequence_List((PyObject*) self))) {
        return NULL;
    }
    ret = PyUnicode_FromFormat("%s(%R)", Py_TYPE(self)->tp_name, aslist);
    Py_DECREF(aslist);
    return ret;
}

PyObject*
sortedmap::keyview::view(sortedmap::object *self) {
    return sortedmap::abstractview::view<sortedmap::keyview::object,
                                         sortedmap::keyview::type>(self);
}

PyObject*
sortedmap::valview::view(sortedmap::object *self) {
    return sortedmap::abstractview::view<sortedmap::valview::object,
                                         sortedmap::valview::type>(self);
}

PyObject*
sortedmap::itemview::view(sortedmap::object *self) {
    return sortedmap::abstractview::view<sortedmap::itemview::object,
                                         sortedmap::itemview::type>(self);
}

void
sortedmap::abstractview::dealloc(sortedmap::abstractview::object *self) {
    using ownedtype = OwnedRef<sortedmap::object>;

    self->map.~ownedtype();
    PyObject_Del(self);
}

static sortedmap::object*
innernew(PyTypeObject *cls, PyObject *keyfunc) {
    sortedmap::object *self = PyObject_GC_New(sortedmap::object, cls);
    ;

    if (unlikely(!self)) {
        return NULL;
    }

    self = new(self) sortedmap::object;
    Py_XINCREF(keyfunc);
    self->map = std::move(sortedmap::maptype(sortedmap::Comparator(keyfunc)));
    return self;
}

sortedmap::object*
sortedmap::newobject(PyTypeObject *cls, PyObject *args, PyObject *kwargs) {
    return innernew(cls, NULL);
}

int
sortedmap::init(sortedmap::object *self, PyObject *args, PyObject *kwargs) {
    return (sortedmap::update(self, args, kwargs)) ? 0 : -1;
}

void
sortedmap::dealloc(sortedmap::object *self) {
    using sortedmap::maptype;

    sortedmap::clear(self);
    self->map.~maptype();
    PyObject_GC_Del(self);
}

int
sortedmap::traverse(sortedmap::object *self, visitproc visit, void *arg) {
    for (const auto &pair : self->map) {
        Py_VISIT(pair.first);
        Py_VISIT(pair.second);
    }
    return 0;
}

void
sortedmap::clear(sortedmap::object *self) {
    self->map.clear();
}

PyObject*
sortedmap::pyclear(sortedmap::object *self) {
    sortedmap::clear(self);
    Py_RETURN_NONE;
}

PyObject *
sortedmap::richcompare(sortedmap::object *self, PyObject *other, int opid) {
    if (!(opid == Py_EQ || opid == Py_NE) || !sortedmap::check(other)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    sortedmap::object *asmap = (sortedmap::object*) other;

    if (self->map.size() != asmap->map.size()) {
        return PyBool_FromLong(opid != Py_EQ);
    }

    int status;

    if ((size_t) self->map.key_comp().keyfunc ^
        (size_t) asmap->map.key_comp().keyfunc) {
        return PyBool_FromLong(opid != Py_EQ);
    }
    else if (self->map.key_comp().keyfunc && asmap->map.key_comp().keyfunc) {
        status = PyObject_RichCompareBool(self->map.key_comp().keyfunc,
                                          asmap->map.key_comp().keyfunc,
                                          opid);
        if (unlikely(status < 0)) {
            return NULL;
        }
        if (!status) {
            return PyBool_FromLong(opid != Py_EQ);
        }
    }

    OwnedRef<PyObject> other_val;

    for (const auto &pair : self->map) {
        try{
            other_val = std::move(asmap->map.at(std::get<0>(pair)));
        }
        catch (std::out_of_range &e) {
            return PyBool_FromLong(opid != Py_EQ);
        }
        catch (PythonError &e) {
            return NULL;
        }
        status = PyObject_RichCompareBool(std::get<1>(pair), other_val, opid);
        if (unlikely(status < 0)) {
            return NULL;
        }
        if (!status) {
            Py_RETURN_FALSE;
        }
    }
    Py_RETURN_TRUE;
}

Py_ssize_t
sortedmap::len(sortedmap::object *self) {
    return self->map.size();
}

PyObject*
sortedmap::getitem(sortedmap::object *self, PyObject *key) {
    try {
        const auto &it = self->map.find(key);
        if (it == self->map.end()) {
            PyErr_SetObject(PyExc_KeyError, key);
            return NULL;
        }
        return std::get<1>(*it).incref();
    }
    catch (PythonError &e) {
        return NULL;
    }
}

PyObject*
sortedmap::get(sortedmap::object *self, PyObject *key, PyObject *def) {
    try {
        const auto &it = self->map.find(key);
        if (it == self->map.end()) {
            Py_INCREF(def);
            return def;
        }
        return std::get<1>(*it).incref();
    }
    catch (PythonError &e) {
        return NULL;
    }
}

PyObject*
sortedmap::pyget(sortedmap::object *self, PyObject *args, PyObject *kwargs) {
    const char *keywords[] = {"key", "default", NULL};
    PyObject *key;
    PyObject *def = NULL;

    if (!PyArg_ParseTupleAndKeywords(args,
                                     kwargs,
                                     "O|O:get",
                                     (char**) keywords,
                                     &key,
                                     &def)) {
        return NULL;
    }

    if (!def) {
        def = Py_None;
    }

    return sortedmap::get(self, key, def);
}

PyObject*
sortedmap::pop(sortedmap::object *self, PyObject *key, PyObject *def) {
    try {
        PyObject *ret;

        const auto &it = self->map.find(key);
        if (it == self->map.end()) {
            if (!def) {
                PyErr_SetObject(PyExc_KeyError, key);
            }
            else {
                Py_INCREF(def);
            }
            return def;
        }
        ret = std::get<1>(*it).incref();
        // use the same iterator to the item for a faster erase
        self->map.erase(it);
        ++self->iter_revision;
        return ret;
    }
    catch (PythonError &e) {
        return NULL;
    }
}

PyObject*
sortedmap::pypop(sortedmap::object *self, PyObject *args, PyObject *kwargs) {
    const char *keywords[] = {"key", "default", NULL};
    PyObject *key;
    PyObject *def = NULL;

    if (!PyArg_ParseTupleAndKeywords(args,
                                     kwargs,
                                     "O|O:pop",
                                     (char**) keywords,
                                     &key,
                                     &def)) {
        return NULL;
    }

    return sortedmap::pop(self, key, def);
}

PyObject*
sortedmap::popitem(sortedmap::object *self, bool front) {
    sortedmap::maptype::iterator it;
    bool empty;
    PyObject *ret;

    if (front) {
        it = self->map.begin();
        empty = it == self->map.end();
    }
    else {
        auto rit = self->map.rbegin();
        empty = rit == self->map.rend();
        it = --rit.base();
    }

    if (empty) {
        PyErr_SetString(PyExc_KeyError, "sortedmap is empty");
        return NULL;
    }

    if (!(ret = sortedmap::itemiter::elem(it))) {
        return NULL;
    }
    ++self->iter_revision;
    self->map.erase(it);
    return ret;
}

PyObject*
sortedmap::pypopitem(sortedmap::object *self,
                     PyObject *args,
                     PyObject *kwargs) {
    const char *keywords[] = {"first", NULL};
    PyObject *pyfirst = NULL;
    int first;

    if (!PyArg_ParseTupleAndKeywords(args,
                                     kwargs,
                                     "|O:popitem",
                                     (char**) keywords,
                                     &pyfirst)) {
        return NULL;
    }

    if (pyfirst) {
        first = PyObject_IsTrue(pyfirst);
        if (first < 0) {
            return NULL;
        }
    }
    else {
        first = true;
    }
    return sortedmap::popitem(self, first);
}

static void
setitem_throws(sortedmap::object *self, PyObject *key, PyObject *value) {
    const auto &pair = self->map.emplace(key, value);
    if (std::get<1>(pair)) {
        ++self->iter_revision;
    }
    else {
        std::get<1>(*std::get<0>(pair)) = std::move(OwnedRef<PyObject>(value));
    }
}

int
sortedmap::setitem(sortedmap::object *self, PyObject *key, PyObject *value) {
    try {
        if (!value) {
            self->map.erase(key);
            ++self->iter_revision;
        }
        else {
            setitem_throws(self, key, value);
        }
    }
    catch (PythonError &e) {
        return -1;
    }
    return 0;
}


PyObject*
sortedmap::setdefault(sortedmap::object *self, PyObject *key, PyObject *def) {
    PyObject *ret;
    try {
        ret = sortedmap::valiter::elem(
            std::get<0>(self->map.emplace(key, def)));
        if (ret != def) {
            ++self->iter_revision;
        }
        return ret;
    }
    catch (PythonError &e) {
        return NULL;
    }
}

PyObject*
sortedmap::pysetdefault(sortedmap::object *self,
                        PyObject *args,
                        PyObject *kwargs) {
    const char *keywords[] = {"key", "default", NULL};
    PyObject *key;
    PyObject *def = NULL;

    if (!PyArg_ParseTupleAndKeywords(args,
                                     kwargs,
                                     "O|O:setdefault",
                                     (char**) keywords,
                                     &key,
                                     &def)) {
        return NULL;
    }

    if (!def) {
        def = Py_None;
    }
    return sortedmap::setdefault(self, key, def);
}

int
sortedmap::contains(sortedmap::object *self, PyObject *key) {
    try {
        return self->map.find(key) != self->map.end();
    }
    catch (PythonError &e) {
        return -1;
    }
}

PyObject*
sortedmap::repr(sortedmap::object *self) {
    PyObject *it;
    PyObject *aslist;
    PyObject *ret;
    PyObject *keyfunc;

    if (!(it = itemiter::iter(self))) {
        return NULL;
    }
    aslist = PySequence_List(it);
    Py_DECREF(it);
    if (!aslist) {
        return NULL;
    }
    if (self->map.key_comp().keyfunc) {
        if (!(keyfunc =
              PyUnicode_FromFormat("[%R]", self->map.key_comp().keyfunc))) {
            Py_DECREF(aslist);
            return NULL;
        }
    }
    else {
        if (!(keyfunc = PyUnicode_FromString(""))) {
            Py_DECREF(aslist);
            return NULL;
        }
    }
    ret = PyUnicode_FromFormat("%s%S(%R)",
                               Py_TYPE(self)->tp_name,
                               keyfunc,
                               aslist);
    Py_DECREF(keyfunc);
    Py_DECREF(aslist);
    return ret;
}

sortedmap::object*
sortedmap::copy(sortedmap::object *self) {
    sortedmap::object *ret = innernew(Py_TYPE(self),
                                      self->map.key_comp().keyfunc);

    if (unlikely(!ret)) {
        return NULL;
    }

    ret->map = self->map;
    return ret;
}

static bool
merge(sortedmap::object *self, PyObject *other) {
    if (sortedmap::check_exact(other)) {
        sortedmap::object *asmap = (sortedmap::object*) other;
        if (!self->map.size() &&
            self->map.key_comp().keyfunc == asmap->map.key_comp().keyfunc) {
            // fast path for copy constructor
            self->map = asmap->map;
            return true;
        }
        try {
            for (const auto &pair : ((sortedmap::object*) other)->map) {
                setitem_throws(self, std::get<0>(pair), std::get<1>(pair));
            }
        }
        catch (PythonError &e) {
            return false;
        }

        return true;
    }

    PyObject *key;

    if (PyDict_Check(other)) {
        PyObject *value;
        Py_ssize_t pos = 0;

        while (PyDict_Next(other, &pos, &key, &value)) {
            try {
                setitem_throws(self, key, value);
            }
            catch (PythonError &e) {
                return false;
            }
        }
    }
    else {
        PyObject *keys;
        PyObject *it;
        PyObject *tmp;

        if (unlikely(!(keys = PyMapping_Keys(other)))) {
            return false;
        }

        it = PyObject_GetIter(keys);
        Py_DECREF(keys);
        if (unlikely(!it)) {
            return false;
        }
        while ((key = PyIter_Next(it))) {
            if (unlikely(!(tmp = PyObject_GetItem(other, key)))) {
                Py_DECREF(key);
                Py_DECREF(it);
                return false;
            }
            try {
                setitem_throws(self, key, tmp);
            }
            catch (PythonError &e) {
                Py_DECREF(key);
                Py_DECREF(it);
                return false;
            }
            Py_DECREF(key);
        }
        Py_DECREF(it);
        if (unlikely(PyErr_Occurred())) {
            return false;
        }
    }
    return true;
}

static bool
merge_from_seq2(sortedmap::object *self, PyObject *seq2) {
    PyObject *it;
    Py_ssize_t n;
    PyObject *item;
    PyObject *fast;

    if (unlikely(!(it = PyObject_GetIter(seq2)))) {
        return false;
    }

    for (n = 0;;++n) {
        PyObject *key, *value;
        Py_ssize_t len;

        fast = NULL;
        if (unlikely(!(item = PyIter_Next(it)))) {
            if (unlikely(PyErr_Occurred())) {
                goto fail;
            }
            break;
        }

        // convert item to sequence, and verify length 2
        if (unlikely(!(fast = PySequence_Fast(item, "")))) {
            if (PyErr_ExceptionMatches(PyExc_TypeError))
                PyErr_Format(PyExc_TypeError,
                             "cannot convert sortedmap update "
                             "sequence element %zd to a sequence",
                             n);
            goto fail;
        }
        len = PySequence_Fast_GET_SIZE(fast);
        if (unlikely(len != 2)) {
            PyErr_Format(PyExc_ValueError,
                         "sortedmap update sequence element %zd "
                         "has length %zd; 2 is required",
                         n, len);
            goto fail;
        }

        // update with this (key, value) pair
        key = PySequence_Fast_GET_ITEM(fast, 0);
        value = PySequence_Fast_GET_ITEM(fast, 1);
        try{
            setitem_throws(self, key, value);
        }
        catch (PythonError &e) {
            goto fail;
        }
        Py_DECREF(fast);
        Py_DECREF(item);
    }

    n = 0;
    goto return_;
fail:
    Py_XDECREF(item);
    Py_XDECREF(fast);
    n = -1;
return_:
    Py_DECREF(it);
    return !Py_SAFE_DOWNCAST(n, Py_ssize_t, int);
}

bool
sortedmap::update(sortedmap::object *self, PyObject *args, PyObject *kwargs) {
    PyObject *arg = NULL;

    if (unlikely(!PyArg_UnpackTuple(args, "update", 0, 1, &arg))) {
        return false;
    }

    if (arg) {
        if (PyObject_HasAttrString(arg, "keys"))
        {

            if (unlikely(!merge(self, arg))) {
                return false;
            }
        }
        else {
            if (unlikely(!merge_from_seq2(self, arg))) {
                return false;
            }
        }
    }
    if (kwargs && PyDict_Size(kwargs)) {
        if (unlikely(!merge(self, kwargs))) {
            return false;
        }
    }
    return true;
}

PyObject*
sortedmap::pyupdate(sortedmap::object *self, PyObject *args, PyObject *kwargs) {
    if (unlikely(!sortedmap::update(self, args, kwargs))) {
        return NULL;
    }
    Py_RETURN_NONE;
}

sortedmap::object*
sortedmap::fromkeys(PyTypeObject *cls, PyObject *seq, PyObject *value) {
    sortedmap::object *self;
    PyObject *it;
    PyObject *key;

    if (unlikely(!(it = PyObject_GetIter(seq)))) {
        return NULL;
    }

    if (unlikely(!(self = innernew(cls, NULL)))) {
        Py_DECREF(it);
        return NULL;
    }

    while ((key = PyIter_Next(it))) {
        self->map.emplace(key, value);
        Py_DECREF(key);
    }
    Py_DECREF(it);
    if (unlikely(PyErr_Occurred())) {
        return NULL;
    }

    return self;
}

sortedmap::object*
sortedmap::pyfromkeys(PyObject *cls, PyObject *args, PyObject *kwargs) {
    const char *keywords[] = {"seq", "value", NULL};
    PyObject *seq;
    PyObject *value = NULL;

    if (!PyArg_ParseTupleAndKeywords(args,
                                     kwargs,
                                     "O|O:fromkeys",
                                     (char**) keywords,
                                     &seq,
                                     &value)) {
        return NULL;
    }

    if (!value) {
        value = Py_None;
    }

    return sortedmap::fromkeys((PyTypeObject*) cls, seq, value);
}

PyObject*
sortedmap::get_iter_revision(object *self) {
    return PyLong_FromUnsignedLong(self->iter_revision);
}

PyObject*
sortedmap::get_keyfunc(object *self) {
    PyObject *ret =self->map.key_comp().keyfunc;
    if (!ret) {
        ret = Py_None;
    }
    Py_INCREF(ret);
    return ret;
}

void
sortedmap::meta::partial::dealloc(sortedmap::meta::partial::object *self) {
    using ownedtype = OwnedRef<PyObject>;
    using ownedcls = OwnedRef<PyTypeObject>;

    self->cls.~ownedcls();
    self->keyfunc.~ownedtype();
    PyObject_GC_Del(self);
}

sortedmap::object*
sortedmap::meta::partial::call(sortedmap::meta::partial::object *self,
                               PyObject *args,
                               PyObject *kwargs) {
    sortedmap::object *m;

    if (!(m = innernew(self->cls, self->keyfunc.ob))) {
        return NULL;
    }
    if (sortedmap::init(m, args, kwargs)) {
        Py_DECREF(m);
        return NULL;
    }
    return m;
}

PyObject*
sortedmap::meta::partial::repr(sortedmap::meta::partial::object *self) {
    return PyUnicode_FromFormat("%s[%R]",
                                self->cls.ob->tp_name,
                                self->keyfunc.ob);
}

int
sortedmap::meta::partial::traverse(sortedmap::meta::partial::object *self,
                                   visitproc visit,
                                   void *arg) {
    Py_VISIT((PyObject*) self->cls.ob);
    Py_VISIT(self->keyfunc);
    return 0;
}

void
sortedmap::meta::partial::clear(sortedmap::meta::partial::object *self) {
    using ownedtype = OwnedRef<PyObject>;
    using ownedcls = OwnedRef<PyTypeObject>;

    self->cls.~ownedcls();
    self->keyfunc.~ownedtype();
}

sortedmap::meta::partial::object*
sortedmap::meta::getitem(PyObject *cls, PyObject *keyfunc) {
    sortedmap::meta::partial::object *partial;

    if (!PyType_Check(cls)) {
        PyErr_Format(PyExc_TypeError, "%R is not a type object", cls);
        return NULL;
    }

    if (!(partial = PyObject_GC_New(sortedmap::meta::partial::object,
                                    &sortedmap::meta::partial::type))) {
        return NULL;
    }
    new(partial) sortedmap::meta::partial::object;
    partial->cls = std::move((PyTypeObject*) cls);
    partial->keyfunc = std::move(keyfunc);
    return partial;
}

#define MODULE_NAME "sortedmap._sortedmap"
PyDoc_STRVAR(module_doc,
             "A sorted map that does not use hashing.");

#if !COMPILING_IN_PY2
static struct PyModuleDef _sortedmap_module = {
    PyModuleDef_HEAD_INIT,
    MODULE_NAME,
    module_doc,
    -1,
};
#endif  // !COMPILING_IN_PY2

extern "C" {
PyMODINIT_FUNC
#if !COMPILING_IN_PY2
#define ERROR_RETURN NULL
PyInit__sortedmap(void)
#else
#define ERROR_RETURN
init_sortedmap(void)
#endif  // !COMPILING_IN_PY2
{
    std::vector<PyTypeObject*> ts = {&sortedmap::meta::partial::type,
                                     &sortedmap::meta::type,
                                     &sortedmap::keyiter::type,
                                     &sortedmap::valiter::type,
                                     &sortedmap::itemiter::type,
                                     &sortedmap::keyview::type,
                                     &sortedmap::valview::type,
                                     &sortedmap::itemiter::type,
                                     &sortedmap::type};
    PyObject *m;

    for (const auto &t : ts) {
        if (PyType_Ready(t)) {
            return ERROR_RETURN;
        }
    }

#if !COMPILING_IN_PY2
    if (!(m = PyModule_Create(&_sortedmap_module)))
#else
    if (!(m = Py_InitModule3(MODULE_NAME, NULL, module_doc)))
#endif  // !COMPILING_IN_PY2
    {
        return ERROR_RETURN;
    }

    if (PyModule_AddObject(m, "sortedmap", (PyObject*) &sortedmap::type)) {
        Py_DECREF(m);
        return ERROR_RETURN;
    }

#if !COMPILING_IN_PY2
    return m;
#endif  // !COMPILING_IN_PY2
}
}
