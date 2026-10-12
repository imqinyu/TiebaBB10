/*
 * Copyright (c) 2011-2015 BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "applicationui.hpp"

#include "TiebaController.hpp"
#include "api/TbSearchService.hpp"
#include "api/TbSignService.hpp"
#include "api/TbFavoriteService.hpp"
#include "model/TbThreadLibrary.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/LocaleHandler>

#include <QNetworkProxyFactory>
#include <QTranslator>

using namespace bb::cascades;

ApplicationUI::ApplicationUI() :
        QObject(),
        m_pTranslator(0),
        m_pLocaleHandler(0),
        m_pTieba(0)
{
    // 使用系统代理设置（企业网络/运营商环境需要）
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    // prepare the localization
    m_pTranslator = new QTranslator(this);
    m_pLocaleHandler = new LocaleHandler(this);

    bool res = QObject::connect(m_pLocaleHandler, SIGNAL(systemLanguageChanged()), this, SLOT(onSystemLanguageChanged()));
    // This is only available in Debug builds
    Q_ASSERT(res);
    // Since the variable is not used in the app, this is added to avoid a
    // compiler warning
    Q_UNUSED(res);

    // initial load
    onSystemLanguageChanged();

    registerQmlTypes();

    // Create scene document from main.qml asset, the parent is set
    // to ensure the document gets destroyed properly at shut down.
    QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(this);

    // 逻辑层入口：QML 里用 tieba.xxx 调用
    m_pTieba = new TiebaController(this);
    qml->setContextProperty("tieba", m_pTieba);
    qml->setContextProperty("threadLibrary", m_pTieba->threadLibrary());
    qml->setContextProperty("favoriteService", m_pTieba->favoriteService());
    qml->setContextProperty("searchService", new tieba::TbSearchService(m_pTieba->apiClient(), m_pTieba->session(), this));
    qml->setContextProperty("signService", new tieba::TbSignService(m_pTieba->httpClient(), m_pTieba->session(), this));

    // Create root object for the UI
    AbstractPane *root = qml->createRootObject<AbstractPane>();

    // Set created root object as the application scene
    Application::instance()->setScene(root);

    // 读取本地登录态。放在 setScene 之后，这样 QML 已经绑定好属性，
    // 能立刻反映登录状态。
    m_pTieba->init();
}

void ApplicationUI::registerQmlTypes()
{
    // 现在数据全部以 QVariantMap / bb::cascades::ArrayDataModel 的形式交给 QML，
    // 不再需要向 QML 注册自定义类型 —— Cascades 的 ListView 只认 DataModel，
    // 注册 QObject 派生类对它没有帮助。
    //
    // 这里保留这个空实现，是为了方便以后需要注册枚举或 QML 组件时有个地方放：
    //     qmlRegisterUncreatableType<...>("com.bbtieba.tieba", 1, 0, "Name", reason);
}

void ApplicationUI::onSystemLanguageChanged()
{
    QCoreApplication::instance()->removeTranslator(m_pTranslator);
    // Initiate, load and install the application translation files.
    QString locale_string = QLocale().name();
    QString file_name = QString("BBTieba_%1").arg(locale_string);
    if (m_pTranslator->load(file_name, "app/native/qm")) {
        QCoreApplication::instance()->installTranslator(m_pTranslator);
    }
}
