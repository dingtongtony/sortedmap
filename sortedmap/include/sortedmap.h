#pragma once
#include <array>
#include <exception>
#include <map>

#include <Python.h>
#include <structmember.h>

#define COMPILING_IN_PY2 (PY_VERSION_HEX <= 0x03000000)

#ifndef Py_RETURN_NOTIMPLEMENTED
#define Py_RETURN_NOTIMPLEMENTED                \
    Py_INCREF(Py_NotImplemented);               \
    return Py_NotImplemented
#endif  // Py_RETURN_NOTIMPLEMENTED

#ifndef Py_TPFLAGS_CHECKTYPES
#define Py_TPFLAGS_CHECKTYPES 0  // ignore this in py3
#endif  // Py_TPFLAGS_CHECKTYPES

#define likely(condition) __builtin_expect(!!(condition), 1)
#define unlikely(condition) __builtin_expect(!!(condition), 0)

class PythonError : std::exception {};

template<typename T>
class OwnedRef final {
private:
    template<int opid>
    bool inline richcompare(const OwnedRef<T> &b) const {
        int result = PyObject_RichCompareBool(ob, b, opid);
        if (unlikely(result < 0)) {
            throw PythonError();
        }
        return result;
    }

    void inline construct(T *ob) {
        if (likely(ob)) {
            Py_INCREF(ob);
        }
        this->ob = ob;
    }

public:
    T *ob;

    OwnedRef<T>() {
        ob = NULL;
    }

    OwnedRef<T>(T *ob) {
        construct(ob);
    }

    OwnedRef<T>(const OwnedRef<T> &ref) : OwnedRef<T>(ref.ob) {}

    OwnedRef<T> &operator=(OwnedRef<T> &&ref) {
        construct(ref.ob);
        return *this;
    }

    ~OwnedRef<T>() {
        if (likely(ob)) {
            Py_DECREF(ob);
        }
    }

    T *incref() const {
        Py_INCREF(ob);
        return ob;
    }

    bool operator<(const OwnedRef<T> &ref) const {
        return richcompare<Py_LT>(ref);
    }

    bool operator<=(const OwnedRef<T> &ref) const {
        return richcompare<Py_LE>(ref);
    }

    bool operator>(const OwnedRef<T> &ref) const {
        return richcompare<Py_GT>(ref);
    }

    bool operator>=(const OwnedRef<T> &ref) const {
        return richcompare<Py_GE>(ref);
    }

    bool operator==(const OwnedRef<T> &ref) const {
        return richcompare<Py_EQ>(ref);
    }

    bool operator!=(const OwnedRef<T> &ref) const {
        return richcompare<Py_NE>(ref);
    }

    constexpr operator T*() const {
        return ob;
    }
};

PyObject *py_identity(PyObject*);

namespace sortedmap {
    class Comparator {
    private:
        PyObject *argtuple;  // not using ownedref for copying issues

        PyObject *call(PyObject*);

    public:
        PyObject* keyfunc;  // not using ownedref for copying issues

        Comparator();
        Comparator(PyObject*);
        ~Comparator();
        Comparator &operator=(const Comparator&);
        bool operator()(const OwnedRef<PyObject>&,
                        const OwnedRef<PyObject>&);
    };

    using maptype = std::map<OwnedRef<PyObject>,
                             OwnedRef<PyObject>,
                             Comparator>;

    struct object {
        PyObject_HEAD
        maptype map;
        // Keep track of operations that may invalidate any iterators.
        unsigned long iter_revision;
    };

    bool check(PyObject*);
    bool check_exact(PyObject*);

    typedef PyObject *iterfunc(object*);
    typedef PyObject *viewfunc(object*);
    object *newobject(PyTypeObject*, PyObject*, PyObject*);
    int init(object*, PyObject*, PyObject*);
    void dealloc(object*);
    int traverse(object*, visitproc, void*);
    void clear(object*);
    PyObject *pyclear(object*);
    PyObject *richcompare(object*, PyObject*, int);
    Py_ssize_t len(object*);
    PyObject *getitem(object*, PyObject*);
    PyObject *get(object*, PyObject*, PyObject*);
    PyObject *pyget(object*, PyObject*, PyObject*);
    PyObject *pop(object*, PyObject*, PyObject*);
    PyObject *pypop(object*, PyObject*, PyObject*);
    PyObject *popitem(object*, bool);
    PyObject *pypopitem(object*, PyObject*, PyObject*);
    int setitem(object*, PyObject*, PyObject*);
    PyObject *setdefault(object*, PyObject*, PyObject*);
    PyObject *pysetdefault(object*, PyObject *, PyObject*);
    int contains(object*, PyObject*);
    PyObject *repr(object*);
    object *copy(object*);
    bool update(object*, PyObject*, PyObject*);
    PyObject *pyupdate(object*, PyObject*, PyObject*);
    object *fromkeys(PyTypeObject*, PyObject*, PyObject*);
    object *pyfromkeys(PyObject*, PyObject*, PyObject*);

    PyDoc_STRVAR(iter_revision_doc,
                 "An internal counter used to invalidate iterators after\n"
                 " the map changes size.\n");

    namespace abstractiter {
        using itertype = maptype::const_iterator;
        typedef PyObject *extract_element(itertype);

        struct object {
            PyObject_HEAD
            itertype iter;
            itertype end;
            OwnedRef<sortedmap::object> map;
            // the revision of the map when this iter was created.
            unsigned long iter_revision;
        };

        void dealloc(object*);

        template<extract_element f>
        PyObject*
        next(object *self) {
            PyObject *ret;

            if (unlikely(self->iter_revision != self->map.ob->iter_revision)) {
                PyErr_SetString(PyExc_RuntimeError,
                                "sortedmap changed size during iteration");
                return NULL;
            }
            if (unlikely(self->iter == self->end)) {
                return NULL;
            }

            ret = f(self->iter);
            self->iter = std::move(std::next(self->iter, 1));
            return ret;
        }

        template<typename iterobject, PyTypeObject &cls>
        PyObject*
        iter(sortedmap::object *self) {
            iterobject *ret = PyObject_New(iterobject, &cls);
            if (!ret) {
                return NULL;
            }

            ret->iter = std::move(self->map.cbegin());
            ret->end = std::move(self->map.cend());
            new(&ret->map) OwnedRef<sortedmap::object>(self);
            ret->iter_revision = self->iter_revision;
            return (PyObject*) ret;
        }

        PyMemberDef members[] = {
            {(char*) "_iter_revision",
             T_ULONG,
             offsetof(object, iter_revision),
             READONLY,
             iter_revision_doc},
            {NULL},
        };

        template<const char *&name, extract_element elem>
        PyTypeObject type = {
            PyVarObject_HEAD_INIT(&PyType_Type, 0)
            name,                                       // tp_name
            sizeof(object),                             // tp_basicsize
            0,                                          // tp_itemsize
            (destructor) dealloc,                       // tp_dealloc
            0,                                          // tp_print
            0,                                          // tp_getattr
            0,                                          // tp_setattr
            0,                                          // tp_reserved
            0,                                          // tp_repr
            0,                                          // tp_as_number
            0,                                          // tp_as_sequence
            0,                                          // tp_as_mapping
            0,                                          // tp_hash
            0,                                          // tp_call
            0,                                          // tp_str
            0,                                          // tp_getattro
            0,                                          // tp_setattro
            0,                                          // tp_as_buffer
            Py_TPFLAGS_DEFAULT,                         // tp_flags
            0,                                          // tp_doc
            0,                                          // tp_traverse
            0,                                          // tp_clear
            0,                                          // tp_richcompare
            0,                                          // tp_weaklistoffset
            (getiterfunc) py_identity,                  // tp_iter
            (iternextfunc) next<elem>,                  // tp_iternext
            0,                                          // tp_methods
            members,                                    // tp_members
        };
    }

    namespace keyiter {
        using object = abstractiter::object;

        abstractiter::extract_element elem;
        iterfunc iter;
        extern const char *name;
        PyTypeObject type = abstractiter::type<name, elem>;
    }

    namespace valiter {
        using object = abstractiter::object;

        abstractiter::extract_element elem;
        iterfunc iter;
        extern const char *name;
        PyTypeObject type = abstractiter::type<name, elem>;
    }

    namespace itemiter {
        using object = abstractiter::object;

        abstractiter::extract_element elem;
        iterfunc iter;
        extern const char *name;
        PyTypeObject type = abstractiter::type<name, elem>;
    }

    namespace abstractview {
        typedef PyObject *strict_func(PyObject*);

        struct object {
            PyObject_HEAD
            OwnedRef<sortedmap::object> map;
        };

        void dealloc(object*);
        PyObject *repr(object*);

        template<typename viewobject, PyTypeObject &cls>
        PyObject*
        view(sortedmap::object *self) {
            viewobject *ret = PyObject_New(viewobject, &cls);
            if (!ret) {
                return NULL;
            }

            new(&ret->map) OwnedRef<sortedmap::object>(self);
            return (PyObject*) ret;
        }

        // Specialize binop based on the function and the strict container.
        // valviews are list like but keyviews and itemviews are set like.
        // We want to implmenent different operations for these sometimes
        // but often they are the same. We also want to fail as early as
        // possible and not put phony functions in the as_number struct that
        // always raise. This makes it easier to understand which operations
        // are valid.
        // The default case pulls the lhs and rhs into the strict container
        // and returns the result of the operation on those.
        template<strict_func strict, binaryfunc op, iterfunc iter>
        struct binop {
            static inline PyObject *g(object *self, PyObject *other) {
                PyObject *it;
                PyObject *lhs;
                PyObject *rhs;
                PyObject *res;

                if (!(it = iter(self->map))) {
                    return NULL;
                }

                lhs = strict(it);
                Py_DECREF(it);
                if (!lhs) {
                    return NULL;
                }

                if (!(rhs = strict(other))) {
                    Py_DECREF(lhs);
                    return NULL;
                }
                res = op(lhs, rhs);
                Py_DECREF(lhs);
                Py_DECREF(rhs);
                return res;
            }

            static PyObject *f(PyObject *self, PyObject *other) {
                return g((object*) self, other);
            }
        };

        // we cannot add sets
        template<iterfunc iter>
        struct binop<PySet_New, PyNumber_Add, iter> {
            static constexpr binaryfunc f = NULL;
        };

        // we cannot multiply sets
        template<iterfunc iter>
        struct binop<PySet_New, PyNumber_Multiply, iter> {
            static constexpr binaryfunc f = NULL;
        };

        // we can multiply lists; however, we do not pull the rhs into
        // the strict container because multiply for lists is list repeat
        template<iterfunc iter>
        struct binop<PySequence_List, PyNumber_Multiply, iter> {
            static inline PyObject *g(object *self, PyObject *rhs) {
                PyObject *it;
                PyObject *lhs;
                PyObject *res;

                if (!(it = iter(self->map))) {
                    return NULL;
                }

                lhs = PySequence_List(it);
                Py_DECREF(it);
                if (!lhs) {
                    return NULL;
                }

                res = PyNumber_Multiply(lhs, rhs);
                Py_DECREF(lhs);
                Py_DECREF(rhs);
                return res;
            }

            static PyObject *f(PyObject *self, PyObject *lhs) {
                return g((object*) self, lhs);
            }
        };

        // we cannot subtract lists
        template<iterfunc iter>
        struct binop<PySequence_List, PyNumber_Subtract, iter> {
            static constexpr binaryfunc f = NULL;
        };

        // we cannot intersect lists
        template<iterfunc iter>
        struct binop<PySequence_List, PyNumber_And, iter> {
            static constexpr binaryfunc f = NULL;
        };

        // we cannot symmetric difference lists
        template<iterfunc iter>
        struct binop<PySequence_List, PyNumber_Xor, iter> {
            static constexpr binaryfunc f = NULL;
        };

        // we cannot union lists
        template<iterfunc iter>
        struct binop<PySequence_List, PyNumber_Or, iter> {
            static constexpr binaryfunc f = NULL;
        };

        template<strict_func strict, iterfunc iter>
        PyObject*
        richcompare(object *self, PyObject *other, int opid) {
            PyObject *it;
            PyObject *lhs;
            PyObject *rhs;
            PyObject *res;

            if (!(it = iter(self->map))) {
                return NULL;
            }

            lhs = strict(it);
            Py_DECREF(it);
            if (!lhs) {
                Py_DECREF(lhs);
                return NULL;
            }

            if (!(rhs = strict(other))) {
                Py_DECREF(lhs);
                return NULL;
            }
            res = PyObject_RichCompare(lhs, rhs, opid);
            Py_DECREF(lhs);
            Py_DECREF(rhs);
            return res;
        }

        template<strict_func strict, iterfunc iter>
        int
        pybool(object *self) {
            PyObject *it;
            PyObject *st;

            if (!(it = iter(self->map))) {
                return -1;
            }
            st = strict(it);
            Py_DECREF(it);
            if (!st) {
                return -1;
            }
            return PyObject_IsTrue(st);
        }

        template<iterfunc iterf>
        PyObject*
        iter(object *self) {
            return iterf(self->map);
        }

        template<strict_func strict, iterfunc iter>
        PyNumberMethods as_number = {
            binop<strict, PyNumber_Add, iter>::f,       // nb_add
            binop<strict, PyNumber_Subtract, iter>::f,  // nb_subtract
            binop<strict, PyNumber_Multiply, iter>::f,  // nb_multiply
#if COMPILING_IN_PY2
            0,                                          // nb_divide
#endif  // COMPILING_IN_PY2
            0,                                          // nb_remainder
            0,                                          // nb_divmod
            0,                                          // nb_power
            0,                                          // nb_negative
            0,                                          // nb_positive
            0,                                          // nb_absolute
            (inquiry) pybool<strict, iter>,             // nb_bool
            0,                                          // nb_invert
            0,                                          // nb_lshift
            0,                                          // nb_rshift
            binop<strict, PyNumber_And, iter>::f,       // nb_and
            binop<strict, PyNumber_Xor, iter>::f,       // nb_xor
            binop<strict, PyNumber_Or, iter>::f,        // nb_or
        };

        template<const char *&name, strict_func strict, iterfunc iterf>
        PyTypeObject type = {
            PyVarObject_HEAD_INIT(&PyType_Type, 0)
            name,                                       // tp_name
            sizeof(object),                             // tp_basicsize
            0,                                          // tp_itemsize
            (destructor) dealloc,                       // tp_dealloc
            0,                                          // tp_print
            0,                                          // tp_getattr
            0,                                          // tp_setattr
            0,                                          // tp_reserved
            (reprfunc) repr,                            // tp_repr
            &as_number<strict, iterf>,                  // tp_as_number
            0,                                          // tp_as_sequence
            0,                                          // tp_as_mapping
            0,                                          // tp_hash
            0,                                          // tp_call
            (reprfunc) repr,                            // tp_str
            0,                                          // tp_getattro
            0,                                          // tp_setattro
            0,                                          // tp_as_buffer
            Py_TPFLAGS_DEFAULT |
            Py_TPFLAGS_CHECKTYPES,                      // tp_flags
            0,                                          // tp_doc
            0,                                          // tp_traverse
            0,                                          // tp_clear
            (richcmpfunc) richcompare<strict, iterf>,   // tp_richcompare
            0,                                          // tp_weaklistoffset
            (getiterfunc) iter<iterf>,                  // tp_iter
        };
    }

    namespace keyview {
        using object = abstractview::object;
        viewfunc view;
        extern const char *name;
        PyTypeObject type = abstractview::type<name,
                                               PySet_New,
                                               keyiter::iter>;
    }

    namespace valview {
        using object = abstractview::object;

        viewfunc view;
        extern const char *name;
        PyTypeObject type = abstractview::type<name,
                                               PySequence_List,
                                               valiter::iter>;
    }

    namespace itemview {
        using object = abstractview::object;

        viewfunc view;
        extern const char *name;
        PyTypeObject type = abstractview::type<name,
                                               PySet_New,
                                               itemiter::iter>;
    }

    PySequenceMethods as_sequence = {
        0,                                          // sq_length
        0,                                          // sq_concat
        0,                                          // sq_repeat
        0,                                          // sq_item
        0,                                          // placeholder
        0,                                          // sq_ass_item
        0,                                          // placeholder
        (objobjproc) contains,                      // sq_contains
    };

    PyMappingMethods as_mapping = {
        (lenfunc) len,                              // mp_length
        (binaryfunc) getitem,                       // mp_subscript
        (objobjargproc) setitem,                    // mp_ass_subscript
    };

    namespace meta {
        namespace partial {
            struct object {
                PyObject_HEAD
                OwnedRef<PyTypeObject> cls;
                OwnedRef<PyObject> keyfunc;
            };

            void dealloc(object*);
            sortedmap::object *call(object*, PyObject *args, PyObject *kwargs);
            PyObject *repr(object*);
            int traverse(object*, visitproc, void*);
            void clear(object*);

            PyDoc_STRVAR(sortedmapmeta_partial_doc,
                         "Partial for the sortedmap class that applies\n"
                         "a key function to new instances.\n");

            PyTypeObject type = {
                PyVarObject_HEAD_INIT(&PyType_Type, 0)
                "sortedmap.sortedmapmeta_partial",          // tp_name
                sizeof(object),                             // tp_basicsize
                0,                                          // tp_itemsize
                (destructor) dealloc,                       // tp_dealloc
                0,                                          // tp_print
                0,                                          // tp_getattr
                0,                                          // tp_setattr
                0,                                          // tp_reserved
                (reprfunc) repr,                            // tp_repr
                0,                                          // tp_as_number
                0,                                          // tp_as_sequence
                0,                                          // tp_as_mapping
                0,                                          // tp_hash
                (ternaryfunc) call,                         // tp_call
                (reprfunc) repr,                            // tp_str
                0,                                          // tp_getattro
                0,                                          // tp_setattro
                0,                                          // tp_as_buffer
                Py_TPFLAGS_DEFAULT,                         // tp_flags
                sortedmapmeta_partial_doc,                  // tp_doc
                (traverseproc) traverse,                    // tp_traverse
                (inquiry) clear,                            // tp_clear
            };
        }

        partial::object *getitem(PyObject*, PyObject*);

        PyMappingMethods as_mapping = {
            0,                                          // mp_length
            (binaryfunc) getitem,                       // mp_subscript
            0,                                          // mp_ass_subscript
        };

        PyDoc_STRVAR(sortedmapmeta_doc,
                     "Metaclass that provides __getitem__ for key functions\n");

        PyTypeObject type = {
            PyVarObject_HEAD_INIT(&PyType_Type, 0)
            "sortedmap.sortedmapmeta",                  // tp_name
            sizeof(PyType_Type.tp_basicsize),           // tp_basicsize
            0,                                          // tp_itemsize
            0,                                          // tp_dealloc
            0,                                          // tp_print
            0,                                          // tp_getattr
            0,                                          // tp_setattr
            0,                                          // tp_reserved
            0,                                          // tp_repr
            0,                                          // tp_as_number
            0,                                          // tp_as_sequence
            &as_mapping,                                // tp_as_mapping
            0,                                          // tp_hash
            0,                                          // tp_call
            0,                                          // tp_str
            0,                                          // tp_getattro
            0,                                          // tp_setattro
            0,                                          // tp_as_buffer
            Py_TPFLAGS_DEFAULT,                         // tp_flags
            sortedmapmeta_doc,                          // tp_doc
            0,                                          // tp_traverse
            0,                                          // tp_clear
            0,                                          // tp_richcompare
            0,                                          // tp_weaklistoffset
            0,                                          // tp_iter
            0,                                          // tp_iternext
            0,                                          // tp_methods
            0,                                          // tp_members
            0,                                          // tp_getset
            &PyType_Type,                               // tp_base
        };
    }

    PyDoc_STRVAR(keys_doc,
                 "Returns\n"
                 "-------\n"
                 "v : key_view\n"
                 "    A set-like object providing a view on map's keys.\n");
    PyDoc_STRVAR(values_doc,
                 "Returns\n"
                 "-------\n"
                 "v : value_view\n"
                 "    A set-like object providing a view on map's values.\n");
    PyDoc_STRVAR(items_doc,
                 "Returns\n"
                 "-------\n"
                 "v : value_view\n"
                 "    A set-like object providing a view on map's items.\n");
    PyDoc_STRVAR(clear_doc,
                 "Remove all items from the map.");
    PyDoc_STRVAR(copy_doc,
                 "Returns\n"
                 "-------\n"
                 "copy : sortedmap\n"
                 "    A shallow copy of this sortedmap.\n");
    PyDoc_STRVAR(update_doc,
                 "Update the sortedmap from a mapping or iterable.\n"
                 "\n"
                 "Parameters\n"
                 "----------\n"
                 "it : iterable[key, value]\n"
                 "**kwargs\n"
                 "    The mappings to update this sortedmap with.\n");
    PyDoc_STRVAR(fromkeys_doc,
                 "Create a new sortedmap with keys from ``seq`` all mapping\n"
                 "to ``value``.\n"
                 "\n"
                 "Parameters\n"
                 "seq : iterable\n"
                 "    The keys to use for the new sortedmap.\n"
                 "value : any\n"
                 "    The value that all the keys will map to.\n"
                 "\n"
                 "Returns\n"
                 "-------\n"
                 "m : sortedmap\n"
                 "    The new sorted map object.\n");
    PyDoc_STRVAR(get_doc,
                 "Lookup a key in the sortedmap. If the key is not present\n"
                 "return ``default`` instead.\n"
                 "\n"
                 "Parameters\n"
                 "----------\n"
                 "key : any\n"
                 "    The key to lookup.\n"
                 "default, optional\n"
                 "    The value to return if ``key`` is not in this map\n"
                 "    This defaults to None.\n"
                 "\n"
                 "Returns\n"
                 "-------\n"
                 "val : any\n"
                 "    self[key] if key in self else default\n");
    PyDoc_STRVAR(pop_doc,
                 "Remove a key in the sortedmap. This method returns the\n"
                 "value associated with the given key.\n"
                 "\n"
                 "Parameters\n"
                 "----------\n"
                 "key : any\n"
                 "    The key to lookup.\n"
                 "default, optional\n"
                 "    The value to return if ``key`` is not in this map\n"
                 "    This defaults to None.\n"
                 "\n"
                 "Returns\n"
                 "-------\n"
                 "val : any\n"
                 "    self[key] if key in self else default\n"
                 "\n"
                 "Raises\n"
                 "------\n"
                 "KeyError\n"
                 "    Raised when ``key`` not in self.\n");
    PyDoc_STRVAR(popitem_doc,
                 "Remove the first or last (key, value) pair.\n"
                 "\n"
                 "Parameters\n"
                 "----------\n"
                 "first : bool, optional\n"
                 "    Should this remove the first pair?\n"
                 "    This defaults to True.\n"
                 "\n"
                 "Returns\n"
                 "-------\n"
                 "pair : tuple[key, value]\n"
                 "    The first or last pair that has been removed from the\n"
                 "    sortedmap.\n"
                 "\n"
                 "Raises\n"
                 "------\n"
                 "KeyError\n"
                 "    Raised when the sortedmap is empty\n");
    PyDoc_STRVAR(setdefault_doc,
                 "Set a default value for a key.\n"
                 "\n"
                 "Parameters\n"
                 "----------\n"
                 "\n"
                 "key : any\n"
                 "    The key to set the default for.\n"
                 "default : any, optional\n"
                 "    The default value to set.\n"
                 "    This defaults to None.\n"
                 "\n"
                 "Returns\n"
                 "-------\n"
                 "value : any\n"
                 "    The value for ``key``. This might not be ``default`` if\n"
                 "    ``key`` was already in the map.\n");

    PyMethodDef methods[] = {
        {"keys", (PyCFunction) keyview::view, METH_NOARGS, keys_doc},
        {"values", (PyCFunction) valview::view, METH_NOARGS, values_doc},
        {"items", (PyCFunction) itemview::view, METH_NOARGS, items_doc},
        {"clear", (PyCFunction) pyclear, METH_NOARGS, clear_doc},
        {"copy", (PyCFunction) copy, METH_NOARGS, copy_doc},
        {"update", (PyCFunction) pyupdate,
         METH_VARARGS | METH_KEYWORDS, update_doc},
        {"fromkeys", (PyCFunction) pyfromkeys,
         METH_CLASS | METH_VARARGS | METH_KEYWORDS, fromkeys_doc},
        {"get", (PyCFunction) pyget, METH_VARARGS | METH_KEYWORDS, get_doc},
        {"pop", (PyCFunction) pypop, METH_VARARGS | METH_KEYWORDS, pop_doc},
        {"popitem", (PyCFunction) pypopitem,
         METH_VARARGS | METH_KEYWORDS, popitem_doc},
        {"setdefault", (PyCFunction) pysetdefault,
         METH_VARARGS | METH_KEYWORDS, setdefault_doc},
        {NULL},
    };

    PyObject *get_iter_revision(object*);
    PyObject *get_keyfunc(object*);

    PyDoc_STRVAR(keyfunc_doc,
                 "The key function used for comparing keys.\n"
                 "If no function was provided this returns None.\n");

    // not using a member because object has a non standard layout
    PyGetSetDef getsets[] = {
        {(char*) "keyfunc",
         (getter) get_keyfunc,
         NULL,
         keyfunc_doc,
         NULL},
        {(char*) "_iter_revision",
         (getter) get_iter_revision,
         NULL,
         iter_revision_doc,
         NULL},
        {NULL},
    };

    PyDoc_STRVAR(sortedmap_doc,
                 "A sorted mapping that does not use hashing.\n"
                 "\n"
                 "Parameters\n"
                 "----------\n"
                 "mapping : mapping\n"
                 "**kwargs\n"
                 "    The initial mapping.\n");

    PyTypeObject type = {
        PyVarObject_HEAD_INIT(&meta::type, 0)
        "sortedmap.sortedmap",                      // tp_name
        sizeof(object),                             // tp_basicsize
        0,                                          // tp_itemsize
        (destructor) dealloc,                       // tp_dealloc
        0,                                          // tp_print
        0,                                          // tp_getattr
        0,                                          // tp_setattr
        0,                                          // tp_reserved
        (reprfunc) repr,                            // tp_repr
        0,                                          // tp_as_number
        &as_sequence,                               // tp_as_sequence
        &as_mapping,                                // tp_as_mapping
        0,                                          // tp_hash
        0,                                          // tp_call
        (reprfunc) repr,                            // tp_str
        0,                                          // tp_getattro
        0,                                          // tp_setattro
        0,                                          // tp_as_buffer
        Py_TPFLAGS_DEFAULT |
        Py_TPFLAGS_BASETYPE |
        Py_TPFLAGS_HAVE_GC,                         // tp_flags
        sortedmap_doc,                              // tp_doc
        (traverseproc) traverse,                    // tp_traverse
        (inquiry) clear,                            // tp_clear
        (richcmpfunc) richcompare,                  // tp_richcompare
        0,                                          // tp_weaklistoffset
        (getiterfunc) keyiter::iter,                // tp_iter
        0,                                          // tp_iternext
        methods,                                    // tp_methods
        0,                                          // tp_members
        getsets,                                    // tp_getset
        0,                                          // tp_base
        0,                                          // tp_dict
        0,                                          // tp_descr_get
        0,                                          // tp_descr_set
        0,                                          // tp_dictoffset
        (initproc) init,                            // tp_init
        0,                                          // tp_alloc
        (newfunc) newobject,                        // tp_new
    };
};
