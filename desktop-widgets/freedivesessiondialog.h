// SPDX-License-Identifier: GPL-2.0
#ifndef FREEDIVESESSIONDIALOG_H
#define FREEDIVESESSIONDIALOG_H

#include <QDialog>
#include <memory>
#include <vector>
#include "core/dive.h"
#include "core/divesite.h"
#include "core/taxonomy.h"

class QButtonGroup;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

class FreediveSessionDialog : public QDialog {
	Q_OBJECT
public:
	FreediveSessionDialog(const dive *session,
			      const std::vector<std::unique_ptr<dive>> &splits,
			      QWidget *parent = nullptr);

	dive_site *existingSite() const;
	std::unique_ptr<dive_site> takeNewSite();

private slots:
	void updateMode();
	void updateOkButton();
	void onSuggestNameClicked();

private:
	void populateCandidates(const dive *session);

	location_t m_location = {};
	taxonomy_data m_taxonomy;
	bool lookupInProgress = false;

	QRadioButton *radioExisting = nullptr;
	QComboBox *comboExisting = nullptr;
	QRadioButton *radioNew = nullptr;
	QLineEdit *nameEdit = nullptr;
	QPushButton *suggestButton = nullptr;
	QLabel *geoStatusLabel = nullptr;
	QDialogButtonBox *buttonBox = nullptr;
};

#endif // FREEDIVESESSIONDIALOG_H
