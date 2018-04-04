import QtQuick 2.6
import QtQuick.Controls 2.2
import com.me 1.0

Item {
    width: 500
    height: 500

    ListView {
        id: myView
        anchors.fill: parent
        model: OutOfProcessDataRepo {
            id: myData
        }
        delegate: Item {
            height: col.height
            width: ListView.view.width

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    myView.currentIndex = index
                }
            }

            Column {
                id: col
                width: parent.width

                Item {
                    width: parent.width
                    height: childrenRect.height

                    Label { anchors.verticalCenter: removeButt.verticalCenter; text: model.lastName + ", " + model.firstName + ": " + model.age + " years old"}

                    Button {
                        id: removeButt
                        anchors.right: parent.right
                        text: "Remove"

                        onClicked: {
                            myData.write("REMOVE " + index + "\n")
                        }
                    }
                }

                Column {
                    height: myView.currentIndex == index ? childrenRect.height : 0
                    visible: height > 0 ? true : false
                    width: childrenRect.width
                    clip: true
                    x: 20
                    spacing: 10

                    TextField {
                        id: firstNameText
                        placeholderText: model.firstName
                    }
                    TextField {
                        id: lastNameText
                        placeholderText: model.lastName
                    }
                    TextField {
                        id: ageText
                        placeholderText: model.age
                    }

                    Button {
                        text: "Update"
                        onClicked: {
                            var dat = JSON.stringify({firstName: firstNameText.text, lastName: lastNameText.text, age: ageText.text})
                            firstNameText.text = ""
                            lastNameText.text = ""
                            ageText.text = ""
                            myData.write("UPDATE " + index + " " + (dat.length) + "\n")
                            myData.write(dat + "\n")
                        }
                    }
                }
            }
        }
    }

    Row {
        anchors.bottom: parent.bottom
        width: parent.width

        Rectangle {
            height: childrenRect.height * 1.5
            width: parent.width
            color: "green"

            Label {
                text: "Add"
                color: "white"
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: 30
                y: height/2
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    myData.write("ADDNEW\n")
                }
            }
        }
    }
}
