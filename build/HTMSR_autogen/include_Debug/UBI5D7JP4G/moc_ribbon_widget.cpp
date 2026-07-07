/****************************************************************************
** Meta object code from reading C++ file 'ribbon_widget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/app_shell/ribbon_widget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ribbon_widget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN5htmsr9app_shell12RibbonWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto htmsr::app_shell::RibbonWidget::qt_create_metaobjectdata<qt_meta_tag_ZN5htmsr9app_shell12RibbonWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "htmsr::app_shell::RibbonWidget",
        "newProjectRequested",
        "",
        "openProjectRequested",
        "importManifestRequested",
        "importCalibrationRequested",
        "importImagesRequested",
        "runReconstructionRequested",
        "retryFailedRequested",
        "exportOutputsRequested",
        "openSettingsRequested"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'newProjectRequested'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openProjectRequested'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'importManifestRequested'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'importCalibrationRequested'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'importImagesRequested'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'runReconstructionRequested'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'retryFailedRequested'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'exportOutputsRequested'
        QtMocHelpers::SignalData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openSettingsRequested'
        QtMocHelpers::SignalData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<RibbonWidget, qt_meta_tag_ZN5htmsr9app_shell12RibbonWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject htmsr::app_shell::RibbonWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5htmsr9app_shell12RibbonWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5htmsr9app_shell12RibbonWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN5htmsr9app_shell12RibbonWidgetE_t>.metaTypes,
    nullptr
} };

void htmsr::app_shell::RibbonWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RibbonWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->newProjectRequested(); break;
        case 1: _t->openProjectRequested(); break;
        case 2: _t->importManifestRequested(); break;
        case 3: _t->importCalibrationRequested(); break;
        case 4: _t->importImagesRequested(); break;
        case 5: _t->runReconstructionRequested(); break;
        case 6: _t->retryFailedRequested(); break;
        case 7: _t->exportOutputsRequested(); break;
        case 8: _t->openSettingsRequested(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::newProjectRequested, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::openProjectRequested, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::importManifestRequested, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::importCalibrationRequested, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::importImagesRequested, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::runReconstructionRequested, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::retryFailedRequested, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::exportOutputsRequested, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (RibbonWidget::*)()>(_a, &RibbonWidget::openSettingsRequested, 8))
            return;
    }
}

const QMetaObject *htmsr::app_shell::RibbonWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *htmsr::app_shell::RibbonWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN5htmsr9app_shell12RibbonWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int htmsr::app_shell::RibbonWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void htmsr::app_shell::RibbonWidget::newProjectRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void htmsr::app_shell::RibbonWidget::openProjectRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void htmsr::app_shell::RibbonWidget::importManifestRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void htmsr::app_shell::RibbonWidget::importCalibrationRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void htmsr::app_shell::RibbonWidget::importImagesRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void htmsr::app_shell::RibbonWidget::runReconstructionRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void htmsr::app_shell::RibbonWidget::retryFailedRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void htmsr::app_shell::RibbonWidget::exportOutputsRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void htmsr::app_shell::RibbonWidget::openSettingsRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}
QT_WARNING_POP
