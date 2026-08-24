// pages/main_page.cpp — 主窗口壳，对齐旧 PySide MainPage。
// 圆角窗口卡片 + 14px 阴影留白 + 36px 自绘标题栏 + 220/64 侧栏动画 + 按需建页。

namespace liveaio::pages {

struct PageMeta {
    QString icon;
    QString name;
    std::function<BasePage*(CoreClient*, QWidget*)> factory;
};

static QVector<PageMeta> pageCatalog() {
    return {
        {QStringLiteral("🏠"), QStringLiteral("主页"),
         [](CoreClient* core, QWidget* parent) -> BasePage* { return new HomePage(core, parent); }},
        {QStringLiteral("⚒"), QStringLiteral("工具"),
         [](CoreClient* core, QWidget* parent) -> BasePage* { return new ToolsPage(core, parent); }},
    };
}

class MainWindow final : public QWidget {
public:
    explicit MainWindow(CoreClient* core, QWidget* parent = nullptr)
        : QWidget(parent), core_(core), metas_(pageCatalog()) {
        setWindowTitle(QStringLiteral("LiveAIO"));
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMinimumSize(900 + kWindowShadowMargin * 2, 600 + kWindowShadowMargin * 2);
        resize(1200 + kWindowShadowMargin * 2, 750 + kWindowShadowMargin * 2);

        pages_.resize(metas_.size());
        pages_.fill(nullptr);
        settingsIndex_ = metas_.size();

        buildUi();
        connectNav();
        navigateMain(0);

        applyTheme();
        liveaio::util::onThemeChange(this, [this](const QString&) { applyTheme(); });
    }

    void onCorePacket(const QJsonObject& packet) {
        // 托盘或第二次启动请求显示界面：本进程已在跑，直接抬起窗口。
        if (packet.value(QStringLiteral("op")).toString() == QStringLiteral("ui.focus")) {
            showNormal();
            raise();
            activateWindow();
            return;
        }
        for (auto* page : pages_) {
            if (page) page->onCorePacket(packet);
        }
        if (settingsPage_) settingsPage_->onCorePacket(packet);
    }

    void onCoreStatus(bool connected) {
        for (auto* page : pages_) {
            if (page) page->onStatusChange(connected);
        }
        if (settingsPage_) settingsPage_->onStatusChange(connected);
    }

    void requestInitialState() {
        if (!core_) return;
        core_->configGet();
        core_->uiCommand(QStringLiteral("login.query"));
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        // 托盘由 Go core 托管。收进托盘时只隐藏窗口：QApplication 必须活到进程结束，
        // 否则托盘再次显示界面就得重建 QApplication，Qt 不支持。
        if (g_minimizeToTray) {
            event->ignore();
            hide();
            if (core_) core_->uiCommand(QStringLiteral("quit.detach_ui"));
            return;
        }
        event->accept();
        if (core_) {
            core_->uiCommand(QStringLiteral("quit.shutdown_all"));
            core_->shutdownCore();
        }
        qApp->quit();
    }

private:
    void buildUi() {
        auto* rootLay = new QVBoxLayout(this);
        rootLay->setContentsMargins(kWindowShadowMargin, kWindowShadowMargin,
                                    kWindowShadowMargin, kWindowShadowMargin);

        card_ = new QFrame(this);
        card_->setObjectName(QStringLiteral("WindowCard"));
        auto* shadow = new QGraphicsDropShadowEffect(card_);
        shadow->setBlurRadius(32);
        shadow->setOffset(0, 4);
        shadow->setColor(QColor(0, 0, 0, 55));
        card_->setGraphicsEffect(shadow);
        rootLay->addWidget(card_);

        auto* cardLay = new QVBoxLayout(card_);
        cardLay->setContentsMargins(0, 0, 0, 0);
        cardLay->setSpacing(0);

        titleBar_ = new TitleBar(card_);
        cardLay->addWidget(titleBar_);

        auto* body = new QWidget(card_);
        auto* bodyLay = new QHBoxLayout(body);
        bodyLay->setContentsMargins(0, 0, 0, 0);
        bodyLay->setSpacing(0);

        QVector<NavItem> navItems;
        for (const PageMeta& meta : metas_) navItems.append(NavItem{meta.icon, meta.name});
        sidebar_ = new Sidebar(navItems, NavItem{QStringLiteral("⚙"), QStringLiteral("设置")}, body);
        bodyLay->addWidget(sidebar_);

        // 物理分隔线：1px 宽，不会被侧栏子控件遮挡。
        auto* divider = new QFrame(body);
        divider->setObjectName(QStringLiteral("SidebarDivider"));
        bodyLay->addWidget(divider);

        stack_ = new QStackedWidget(body);
        stack_->setObjectName(QStringLiteral("ContentArea"));
        for (int i = 0; i < metas_.size(); ++i) stack_->addWidget(new QWidget(stack_));
        stack_->addWidget(new QWidget(stack_));
        bodyLay->addWidget(stack_, 1);

        cardLay->addWidget(body, 1);
    }

    void connectNav() {
        const auto& navBtns = sidebar_->navButtons();
        for (int i = 0; i < navBtns.size(); ++i) {
            QObject::connect(navBtns[i], &QPushButton::clicked, this, [this, i]() { navigateMain(i); });
        }
        if (auto* settingsBtn = sidebar_->settingsButton()) {
            QObject::connect(settingsBtn, &QPushButton::clicked, this, [this]() { navigateSettings(); });
        }
    }

    void replaceStackWidget(int index, QWidget* widget) {
        QWidget* old = stack_->widget(index);
        stack_->removeWidget(old);
        if (old) old->deleteLater();
        stack_->insertWidget(index, widget);
    }

    BasePage* ensurePage(int index) {
        if (index == settingsIndex_) {
            if (!settingsPage_) {
                settingsPage_ = new SettingsPage(core_, stack_);
                replaceStackWidget(index, settingsPage_);
            }
            return settingsPage_;
        }
        if (index < 0 || index >= metas_.size()) return nullptr;
        if (!pages_[index]) {
            pages_[index] = metas_[index].factory(core_, stack_);
            replaceStackWidget(index, pages_[index]);
        }
        return pages_[index];
    }

    void navigateMain(int index) {
        ensurePage(index);
        stack_->setCurrentIndex(index);
        sidebar_->setActiveMain(index);
    }

    void navigateSettings() {
        ensurePage(settingsIndex_);
        stack_->setCurrentIndex(settingsIndex_);
        sidebar_->setActiveSettings();
    }

    void applyTheme() {
        qApp->setStyleSheet(shellQss());
        sidebar_->refreshTheme();
        for (auto* page : pages_) {
            if (page) page->refreshTheme();
        }
        if (settingsPage_) settingsPage_->refreshTheme();
    }

    CoreClient* core_ = nullptr;
    QVector<PageMeta> metas_;
    QFrame* card_ = nullptr;
    TitleBar* titleBar_ = nullptr;
    Sidebar* sidebar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QVector<BasePage*> pages_;
    SettingsPage* settingsPage_ = nullptr;
    int settingsIndex_ = 0;
};

}  // namespace liveaio::pages
