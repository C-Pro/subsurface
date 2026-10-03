// SPDX-License-Identifier: GPL-2.0
#include "desktop-widgets/freedivesessiondialog.h"
#include "core/divelog.h"
#include "core/divesitehelpers.h"
#include "core/string-format.h"
#include "core/units.h"

#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QApplication>
#include <algorithm>

FreediveSessionDialog::FreediveSessionDialog(const dive *session,
					     const std::vector<std::unique_ptr<dive>> &splits,
					     QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Process Freedive Session into Trip"));
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	// Summary Group
	QGroupBox *summaryGroup = new QGroupBox(tr("Freedive Session Summary"), this);
	QFormLayout *summaryLayout = new QFormLayout(summaryGroup);

	depth_t maxDepth = 0_m;
	for (const auto &sp : splits) {
		if (sp->maxdepth.mm > maxDepth.mm)
			maxDepth = sp->maxdepth;
	}

	summaryLayout->addRow(tr("Freedives found:"),
		new QLabel(tr("%n dive(s)", "", static_cast<int>(splits.size())), this));
	if (session) {
		summaryLayout->addRow(tr("Session duration:"),
			new QLabel(get_duration_string_short(session->duration), this));
	}
	summaryLayout->addRow(tr("Max depth:"),
		new QLabel(get_depth_string(maxDepth, true), this));

	m_location = session ? session->get_gps_location() : location_t{};
	if (has_location(&m_location)) {
		summaryLayout->addRow(tr("GPS coordinates:"),
			new QLabel(printGPSCoords(&m_location), this));
	} else {
		summaryLayout->addRow(tr("GPS coordinates:"),
			new QLabel(tr("Session has no GPS location"), this));
	}
	mainLayout->addWidget(summaryGroup);

	// Dive Site Selection Group
	QGroupBox *siteGroup = new QGroupBox(tr("Dive Site & Trip Location"), this);
	QVBoxLayout *siteLayout = new QVBoxLayout(siteGroup);

	QButtonGroup *modeGroup = new QButtonGroup(this);
	radioExisting = new QRadioButton(tr("Use nearby existing dive site:"), this);
	comboExisting = new QComboBox(this);
	radioNew = new QRadioButton(tr("Create new dive site:"), this);
	modeGroup->addButton(radioExisting);
	modeGroup->addButton(radioNew);

	siteLayout->addWidget(radioExisting);
	siteLayout->addWidget(comboExisting);
	siteLayout->addSpacing(8);
	siteLayout->addWidget(radioNew);

	QHBoxLayout *nameLayout = new QHBoxLayout();
	nameEdit = new QLineEdit(this);
	nameEdit->setPlaceholderText(tr("Enter dive site name..."));
	suggestButton = new QPushButton(tr("Suggest Name"), this);
	nameLayout->addWidget(nameEdit);
	nameLayout->addWidget(suggestButton);
	siteLayout->addLayout(nameLayout);

	geoStatusLabel = new QLabel(this);
	geoStatusLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
	siteLayout->addWidget(geoStatusLabel);

	mainLayout->addWidget(siteGroup);

	// Pre-fill taxonomy if session->dive_site already has it and locations match
	if (session && session->dive_site && !session->dive_site->taxonomy.empty()) {
		if (!has_location(&m_location) ||
		    (session->dive_site->has_gps_location() && session->dive_site->location == m_location)) {
			m_taxonomy = session->dive_site->taxonomy;
			std::string local = taxonomy_get_value(m_taxonomy, TC_LOCALNAME);
			if (local.empty())
				local = taxonomy_get_value(m_taxonomy, TC_ADMIN_L3);
			if (!local.empty())
				nameEdit->setText(QString::fromStdString(local).trimmed());
		}
	}

	populateCandidates(session);

	// Dialog buttons
	buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	mainLayout->addWidget(buttonBox);

	connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(radioExisting, &QRadioButton::toggled, this, &FreediveSessionDialog::updateMode);
	connect(radioNew, &QRadioButton::toggled, this, &FreediveSessionDialog::updateMode);
	connect(comboExisting, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FreediveSessionDialog::updateOkButton);
	connect(nameEdit, &QLineEdit::textChanged, this, &FreediveSessionDialog::updateOkButton);
	connect(suggestButton, &QPushButton::clicked, this, &FreediveSessionDialog::onSuggestNameClicked);

	updateMode();
}

void FreediveSessionDialog::populateCandidates(const dive *session)
{
	static const QRegularExpression coordRegex(QStringLiteral("^[+-]?\\d+\\.\\d+,\\s*[+-]?\\d+\\.\\d+$"));

	struct CandidateSite {
		dive_site *site;
		unsigned int distance;
	};
	std::vector<CandidateSite> candidates;

	if (has_location(&m_location)) {
		for (const auto &ds : divelog.sites) {
			if (!ds)
				continue;
			QString name = QString::fromStdString(ds->name).trimmed();
			if (name.isEmpty() || coordRegex.match(name).hasMatch())
				continue;
			if (!ds->has_gps_location())
				continue;
			unsigned int dist = get_distance(ds->location, m_location);
			if (dist <= 1000)
				candidates.push_back(CandidateSite{ds.get(), dist});
		}
	} else if (session && session->dive_site) {
		// Session has no GPS: offer current site if it has a non-coordinate name
		QString siteName = QString::fromStdString(session->dive_site->name).trimmed();
		if (!siteName.isEmpty() && !coordRegex.match(siteName).hasMatch()) {
			candidates.push_back(CandidateSite{session->dive_site, 0});
		}
	}

	std::sort(candidates.begin(), candidates.end(), [](const CandidateSite &a, const CandidateSite &b) {
		if (a.distance != b.distance)
			return a.distance < b.distance;
		return a.site->name < b.site->name;
	});

	comboExisting->clear();
	for (const auto &c : candidates) {
		QString label;
		if (has_location(&m_location) && c.site->has_gps_location()) {
			label = QStringLiteral("%1 (%2 m)").arg(QString::fromStdString(c.site->name)).arg(c.distance);
		} else {
			label = QString::fromStdString(c.site->name);
		}
		comboExisting->addItem(label, QVariant::fromValue(c.site));
	}

	if (candidates.empty()) {
		radioExisting->setEnabled(false);
		comboExisting->setEnabled(false);
		radioNew->setChecked(true);
	} else {
		radioExisting->setEnabled(true);
		radioExisting->setChecked(true);
		comboExisting->setEnabled(true);
		comboExisting->setCurrentIndex(0);
	}
}

void FreediveSessionDialog::updateMode()
{
	if (lookupInProgress) {
		if (buttonBox && buttonBox->button(QDialogButtonBox::Ok))
			buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
		suggestButton->setEnabled(false);
		return;
	}
	bool useExisting = radioExisting->isChecked() && radioExisting->isEnabled();
	comboExisting->setEnabled(useExisting);
	nameEdit->setEnabled(!useExisting);
	suggestButton->setEnabled(!useExisting && has_location(&m_location));
	updateOkButton();
}

void FreediveSessionDialog::updateOkButton()
{
	if (lookupInProgress) {
		if (buttonBox && buttonBox->button(QDialogButtonBox::Ok))
			buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
		return;
	}
	bool ok = false;
	if (radioExisting->isChecked() && radioExisting->isEnabled()) {
		ok = (comboExisting->currentIndex() >= 0 &&
		      comboExisting->currentData().value<dive_site *>() != nullptr);
	} else if (radioNew->isChecked()) {
		ok = !nameEdit->text().trimmed().isEmpty();
	}
	if (buttonBox && buttonBox->button(QDialogButtonBox::Ok))
		buttonBox->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

void FreediveSessionDialog::onSuggestNameClicked()
{
	if (!has_location(&m_location) || lookupInProgress)
		return;

	lookupInProgress = true;
	suggestButton->setEnabled(false);
	if (buttonBox && buttonBox->button(QDialogButtonBox::Ok))
		buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
	geoStatusLabel->setText(tr("Looking up location..."));

	QString textBeforeLookup = nameEdit->text();
	QPointer<FreediveSessionDialog> guard(this);

	taxonomy_data resultTaxonomy = reverseGeoLookup(m_location.lat, m_location.lon);

	if (!guard || !isVisible())
		return;

	lookupInProgress = false;
	m_taxonomy = std::move(resultTaxonomy);

	std::string val = taxonomy_get_value(m_taxonomy, TC_LOCALNAME);
	if (val.empty())
		val = taxonomy_get_value(m_taxonomy, TC_ADMIN_L3);
	if (val.empty())
		val = taxonomy_get_value(m_taxonomy, TC_ADMIN_L2);
	if (val.empty())
		val = taxonomy_get_value(m_taxonomy, TC_ADMIN_L1);
	if (val.empty())
		val = taxonomy_get_value(m_taxonomy, TC_OCEAN);
	if (val.empty())
		val = taxonomy_get_value(m_taxonomy, TC_COUNTRY);

	if (!val.empty()) {
		if (nameEdit->text() == textBeforeLookup || nameEdit->text().trimmed().isEmpty())
			nameEdit->setText(QString::fromStdString(val).trimmed());
		geoStatusLabel->setText(tr("Suggested: %1").arg(QString::fromStdString(val)));
	} else {
		geoStatusLabel->setText(tr("No location name found."));
	}

	updateMode();
	updateOkButton();
}

dive_site *FreediveSessionDialog::existingSite() const
{
	if (radioExisting->isChecked() && radioExisting->isEnabled() && comboExisting->currentIndex() >= 0)
		return comboExisting->currentData().value<dive_site *>();
	return nullptr;
}

std::unique_ptr<dive_site> FreediveSessionDialog::takeNewSite()
{
	if (!radioNew->isChecked())
		return nullptr;
	QString name = nameEdit->text().trimmed();
	if (name.isEmpty())
		return nullptr;
	auto site = std::make_unique<dive_site>(name.toStdString(), m_location);
	site->taxonomy = m_taxonomy;
	return site;
}
